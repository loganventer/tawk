import { createHash } from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import makeWASocket, { Browsers, DisconnectReason, jidNormalizedUser, useMultiFileAuthState } from 'baileys';
import qrcode from 'qrcode-terminal';
import { EditReceipts } from './edit-receipts.js';
import { emit } from './protocol.js';
import { toProtocolMessage, statusName, chatJid, aliasesFromKey, describe, parseMediaRef } from './translate.js';
import { isJid, isMessageId, digitsOnly } from './validate.js';
import { fetchLinkPreview } from './link-preview.js';

/** The context that marks a message as forwarded. */
const forwardContext = (score) => ({ isForwarded: true, forwardingScore: Math.max(1, Number(score) || 1) });

/**
 * One WhatsApp login. Never reconnects by itself: it reports why the
 * connection closed and the C messaging manager decides when to reconnect
 * (exponential backoff with jitter, circuit breaker).
 */
export class Session {
    constructor({ authDir, mediaDir, logger, downloader }) {
        this.authDir = authDir;
        this.mediaDir = mediaDir;
        this.logger = logger;
        this.downloader = downloader;
        this.sock = null;
        this.connecting = null;       // in-flight connect(), so two quick calls open one socket
        this.open = false;
        this.qrSeen = null;
        this.unread = new Map();
        this.alternate = new Map();   // phone-number JID <-> LID, for history requests
        this.edits = new EditReceipts();
    }

    /** Remembers and reports a LID to phone-number pair. */
    emitAlias(lid, pn) {
        if (this.alternate.size > 50000) this.alternate.clear();
        this.alternate.set(lid, pn);
        this.alternate.set(pn, lid);
        emit({ evt: 'alias', lid, pn });
    }

    connect() {
        if (this.sock) return Promise.resolve();
        if (!this.connecting) this.connecting = this.openSocket().finally(() => { this.connecting = null; });
        return this.connecting;
    }

    /** Drops the current socket, working or not, and opens a new one. Asked
     * for when the machine's network changes, since a socket on the old
     * adapter can hang until Baileys' keep-alive gives up. */
    async reconnect() {
        await this.connecting?.catch(() => {});
        const old = this.sock;
        this.sock = null;
        this.open = false;
        old?.end(undefined);
        await this.connect();
    }

    async openSocket() {
        await fs.mkdir(this.authDir, { recursive: true, mode: 0o700 });
        const { state, saveCreds } = await useMultiFileAuthState(this.authDir);
        if (!state.creds.registered) emit({ evt: 'auth_required' });
        emit({ evt: 'connection', reason: 'connecting', detail: 'Connecting to WhatsApp' });

        let qrReady;
        this.qrSeen = new Promise((resolve) => { qrReady = resolve; });
        const sock = makeWASocket({
            auth: state,
            logger: this.logger,
            browser: Browsers.ubuntu('Chrome'),
            markOnlineOnConnect: false,
            printQRInTerminal: false,
        });
        this.sock = sock;
        sock.ev.on('creds.update', saveCreds);

        sock.ev.on('connection.update', (update) => {
            if (update.qr) {
                qrcode.generate(update.qr, { small: true }, (ascii) => emit({ evt: 'qr', ascii }));
                qrReady();
            }
            if (update.connection === 'open') {
                this.open = true;
                emit({ evt: 'connected', jid: jidNormalizedUser(sock.user?.id ?? ''), name: sock.user?.name ?? '' });
                emit({ evt: 'connection', reason: 'open', detail: 'Connected' });
                this.syncGroups().catch(() => {});
                this.publishBlocklist().catch(() => {});
            }
            if (update.connection === 'close') this.onClose(sock, update.lastDisconnect?.error);
        });

        sock.ev.on('messages.upsert', ({ messages, type }) => {
            const live = type === 'notify';
            for (const message of messages) {
                this.emitAliases(message.key);
                const protocol = message.message?.protocolMessage;
                if (protocol?.key?.id && (protocol.type === 14 || protocol.type === 0)) {
                    const edited = protocol.type === 14;                  /* 14 MESSAGE_EDIT, 0 REVOKE */
                    emit({ evt: 'edit', id: protocol.key.id, chat: chatJid(message.key), deleted: !edited,
                           ...(edited ? { text: describe({ message: protocol.editedMessage })?.text ?? '' } : {}) });
                    continue;
                }
                const reaction = message.message?.reactionMessage;
                if (reaction?.key?.id) {
                    emit({ evt: 'reaction', id: reaction.key.id, chat: chatJid(message.key),
                           sender: jidNormalizedUser(message.key.participant ?? message.key.remoteJid ?? ''), emoji: reaction.text ?? '' });
                    continue;
                }
                const event = toProtocolMessage(message, live, this.ownIds());
                if (!event) continue;
                emit(event);
                if (live && !event.from_me) this.trackUnread(message);
            }
        });

        sock.ev.on('messaging-history.set', ({ chats, contacts, messages }) => {
            for (const chat of chats ?? []) this.emitChat(chat);
            for (const contact of contacts ?? []) this.emitContact(contact);
            for (const message of messages ?? []) {
                this.emitAliases(message.key);
                const event = toProtocolMessage(message, false, this.ownIds());
                if (event) emit(event);
            }
        });

        sock.ev.on('blocklist.update', () => { this.publishBlocklist().catch(() => {}); });
        sock.ev.on('call', (calls) => {
            for (const c of calls ?? []) {
                if (!isMessageId(c.id)) continue;
                const state = c.status === 'offer' ? 'offer' : c.status === 'accept' ? 'accepted' : 'ended';
                emit({ evt: 'call', id: c.id, from: jidNormalizedUser(c.from), state, video: Boolean(c.isVideo),
                       group: Boolean(c.isGroup), ts: Math.floor(Number(c.date ?? Date.now()) / 1000) });
            }
        });
        // A chat deleted on another device.
        sock.ev.on('chats.delete', (ids) => {
            for (const jid of ids ?? []) if (isJid(jid)) emit({ evt: 'chat_removed', jid: chatJid({ remoteJid: jid }) });
        });
        // Deleted for me on another device.
        sock.ev.on('messages.delete', (item) => {
            for (const key of item?.keys ?? []) {
                if (isMessageId(key?.id)) emit({ evt: 'removed', id: key.id, chat: chatJid(key) });
            }
        });
        sock.ev.on('messages.update', (updates) => {
            for (const { key, update } of updates) {
                const status = statusName(update?.status);
                if (status && key?.fromMe) emit({ evt: 'status', id: this.edits.resolve(key.id), status });
            }
        });
        // Who got how far with each message you sent, for the message info panel.
        sock.ev.on('message-receipt.update', (updates) => {
            for (const { key, receipt } of updates ?? []) {
                if (!key?.fromMe || !isMessageId(key?.id) || !receipt?.userJid) continue;
                const id = this.edits.resolve(key.id);
                const seconds = (t) => Number(t?.low ?? t ?? 0);
                for (const [kind, at] of [['delivered', receipt.receiptTimestamp], ['read', receipt.readTimestamp],
                                          ['played', receipt.playedTimestamp]]) {
                    if (seconds(at) > 0) emit({ evt: 'receipt', id, by: receipt.userJid, kind, at: seconds(at) });
                }
            }
        });

        sock.ev.on('presence.update', ({ id, presences }) => {
            for (const [participant, p] of Object.entries(presences ?? {})) {
                const known = p.lastKnownPresence;
                // Coming online or leaving is its own event; leaving also ends any typing notice.
                if (known === 'available' || known === 'unavailable') {
                    emit({ evt: 'presence', jid: jidNormalizedUser(participant),
                           state: known === 'available' ? 'online' : 'offline', last_seen: Number(p.lastSeen) || 0 });
                    if (known === 'available') continue;
                }
                const state = known === 'composing' ? 'composing' : known === 'recording' ? 'recording' : 'paused';
                emit({ evt: 'typing', chat: chatJid({ remoteJid: id }), sender: jidNormalizedUser(participant), state });
            }
        });
        sock.ev.on('chats.phoneNumberShare', ({ lid, jid }) => {
            if (lid && jid) this.emitAlias(jidNormalizedUser(lid), jidNormalizedUser(jid));
        });
        sock.ev.on('chats.upsert', (chats) => chats.forEach((c) => this.emitChat(c)));
        sock.ev.on('chats.update', (chats) => chats.forEach((c) => this.emitChat(c)));
        sock.ev.on('contacts.upsert', (list) => list.forEach((c) => this.emitContact(c)));
        sock.ev.on('contacts.update', (list) => list.forEach((c) => this.emitContact(c)));
        sock.ev.on('groups.update', (list) => list.forEach((g) => g.subject && emit({ evt: 'chat', jid: g.id, name: g.subject, unread: -1 })));
    }

    onClose(sock, error) {
        if (sock !== this.sock) return;   // a socket already replaced by freshQr or reconnect
        const code = error?.output?.statusCode;
        this.sock = null;
        this.open = false;
        if (code === DisconnectReason.loggedOut) {
            fs.rm(this.authDir, { recursive: true, force: true }).finally(() => emit({ evt: 'logged_out' }));
            return;
        }
        if (code === DisconnectReason.restartRequired) {
            emit({ evt: 'connection', reason: 'restart_required', detail: 'Finishing login' });
            return;
        }
        if (code === DisconnectReason.connectionReplaced) {
            emit({ evt: 'connection', reason: 'replaced', detail: 'Another WhatsApp Web session took over this login.' });
            return;
        }
        emit({ evt: 'connection', reason: 'closed', detail: error?.message ? `Connection closed: ${error.message}` : 'The connection to WhatsApp dropped.' });
    }

    /** Reports LID to phone-number pairs so tawk can merge duplicate chats. */
    emitAliases(key) {
        for (const { lid, pn } of aliasesFromKey(key)) this.emitAlias(lid, pn);
    }

    emitChat(chat) {
        if (!chat?.id) return;
        emit({
            evt: 'chat',
            jid: chatJid({ remoteJid: chat.id }),
            name: chat.name ?? chat.subject ?? '',
            unread: typeof chat.unreadCount === 'number' ? chat.unreadCount : -1,
            ...(typeof chat.archived === 'boolean' ? { archived: chat.archived } : {}),
            ts: Number(chat.conversationTimestamp?.low ?? chat.conversationTimestamp ?? 0),
        });
    }

    emitContact(contact) {
        if (!contact?.id) return;
        emit({ evt: 'contact', jid: jidNormalizedUser(contact.id), name: contact.name ?? '', push_name: contact.notify ?? '' });
    }

    async syncGroups() {
        const groups = await this.sock?.groupFetchAllParticipating();
        for (const [jid, group] of Object.entries(groups ?? {})) {
            emit({ evt: 'chat', jid, name: group.subject ?? '', unread: -1 });
        }
    }

    trackUnread(message) {
        const chat = message.key.remoteJid;
        const keys = this.unread.get(chat) ?? [];
        if (keys.length < 500) keys.push(message.key);
        this.unread.set(chat, keys);
    }

    async freshQr() {
        await this.connecting?.catch(() => {});
        const old = this.sock;
        this.sock = null;
        old?.end(undefined);
        await this.connect();
    }

    async pair(phone) {
        const digits = digitsOnly(phone);
        if (digits.length < 8 || digits.length > 15) {
            emit({ evt: 'error', detail: 'Enter the full number with country code, e.g. 27821234567.' });
            return;
        }
        if (!this.sock) await this.connect();
        await Promise.race([this.qrSeen, new Promise((r) => setTimeout(r, 10000))]);
        const code = await this.sock.requestPairingCode(digits);
        emit({ evt: 'pairing_code', code });
    }

    /** This account's phone JID and LID, to spot mentions of you. */
    ownIds() {
        return { pn: this.sock?.user?.id ?? '', lid: this.sock?.user?.lid ?? '' };
    }

    /** The JIDs to mention and the text to send: groups that address members
     * by LID want the LID both in the mention list and after "@" in the text. */
    async mentionTargets(jid, text, mentions) {
        let lidGroup = false;
        if (jid.endsWith('@g.us')) {
            try { lidGroup = (await this.sock.groupMetadata(jid))?.addressingMode === 'lid'; } catch { lidGroup = false; }
        }
        const jids = [];
        for (const raw of mentions.filter(isJid).slice(0, 32)) {
            let target = raw;
            const lid = lidGroup && raw.endsWith('@s.whatsapp.net') ? this.alternate.get(raw) : null;
            if (lid) {
                text = text.split('@' + raw.split('@')[0]).join('@' + lid.split('@')[0]);
                target = lid;
            }
            jids.push(target);
        }
        return { text, jids };
    }

    /** Likes someone's status: a heart only its author sees (in their viewers list). */
    async likeStatus({ jid, id, emoji }) {
        if (!this.open || !isJid(jid) || !isMessageId(id)) return;
        const me = jidNormalizedUser(this.sock.user?.id ?? '');
        const key = { remoteJid: 'status@broadcast', id, fromMe: false, participant: jid };
        await this.sock.sendMessage('status@broadcast', { react: { text: emoji || '\u2764\ufe0f', key } },
                                    { statusJidList: [jid, me].filter(Boolean) });
    }

    async send(cmd) {
        const { jid, text, id } = cmd;
        if (!this.open || !isJid(jid) || typeof text !== 'string' || !text || text.length > 65536) {
            emit({ evt: 'status', id, status: 'failed' });
            return;
        }
        try {
            const options = isMessageId(id) ? { messageId: id } : {};
            const reply = cmd.reply_to;
            if (reply && isMessageId(reply.id)) {
                options.quoted = {
                    /* a reply to a status quotes it from status@broadcast, as the phone does */
                    key: { remoteJid: reply.status ? 'status@broadcast' : jid, id: reply.id, fromMe: false, participant: reply.sender || undefined },
                    message: { conversation: String(reply.text ?? '').slice(0, 300) },
                };
            }
            const content = { text };
            const link = cmd.link_preview ? text.match(/https:\/\/[^\s<>"]+/i)?.[0]?.replace(/[.,;:!?)\]'"]+$/, '') : null;
            const preview = link ? await fetchLinkPreview(link) : null;
            if (preview) content.linkPreview = preview;
            if (Array.isArray(cmd.mentions) && cmd.mentions.length) {
                const target = await this.mentionTargets(jid, text, cmd.mentions);
                content.text = target.text;
                content.mentions = target.jids;
            }
            if (cmd.forwarded) content.contextInfo = forwardContext(cmd.forwarding_score);
            await this.sock.sendMessage(jid, content, options);
            emit({ evt: 'status', id, status: 'sent' });
            if (preview) {
                emit({ evt: 'link', id, url: preview['matched-text'], title: preview.title, desc: preview.description,
                       ...(preview.jpegThumbnail ? { thumb: Buffer.from(preview.jpegThumbnail).toString('base64') } : {}) });
            }
        } catch {
            emit({ evt: 'status', id, status: 'failed' });
        }
    }

    async sendVoice({ jid, path: file, seconds, id }) {
        const fail = () => emit({ evt: 'status', id, status: 'failed' });
        if (!this.open || !isJid(jid) || !isMessageId(id) || !(await this.insideMediaDir(file))) return fail();
        try {
            const stat = await fs.stat(file);
            if (!stat.isFile() || stat.size === 0 || stat.size > 16 * 1024 * 1024) return fail();
            await this.sock.sendMessage(jid, {
                audio: await fs.readFile(file),
                mimetype: 'audio/ogg; codecs=opus',
                ptt: true,
                seconds: Math.max(1, Number(seconds) || 1),
            }, { messageId: id });
            emit({ evt: 'status', id, status: 'sent' });
        } catch {
            fail();
        }
    }

    async sendMedia({ jid, path: file, kind, mime, file_name: fileName, text, id, forwarded, forwarding_score: score }) {
        const fail = () => emit({ evt: 'status', id, status: 'failed' });
        if (!this.open || !isJid(jid) || !isMessageId(id) || !(await this.insideMediaDir(file))) return fail();
        try {
            const stat = await fs.stat(file);
            if (!stat.isFile() || stat.size === 0 || stat.size > 100 * 1024 * 1024) return fail();
            const data = await fs.readFile(file);
            const caption = typeof text === 'string' && text ? text : undefined;
            const content = kind === 'image' ? { image: data, caption, mimetype: mime }
                : kind === 'video' ? { video: data, caption, mimetype: mime }
                : kind === 'audio' ? { audio: data, mimetype: mime }
                : { document: data, mimetype: mime, fileName: fileName || 'file', caption };
            if (forwarded) content.contextInfo = forwardContext(score);
            await this.sock.sendMessage(jid, content, { messageId: id });
            emit({ evt: 'status', id, status: 'sent' });
        } catch {
            fail();
        }
    }

    /** Sends media WhatsApp already holds (a received message's reference)
     * to another chat, marked as forwarded, without uploading it again. */
    async forwardMedia({ jid, ref, id }) {
        const fail = () => emit({ evt: 'status', id, status: 'failed' });
        if (!this.open || !isJid(jid) || !isMessageId(id) || typeof ref !== 'string' || !ref) return fail();
        try {
            await this.sock.sendMessage(jid, { forward: parseMediaRef(ref), force: true }, { messageId: id });
            emit({ evt: 'status', id, status: 'sent' });
        } catch {
            fail();
        }
    }

    /** Rejects any path outside the media folder. */
    async insideMediaDir(file) {
        try {
            const root = await fs.realpath(this.mediaDir);
            const real = await fs.realpath(String(file));
            return real.startsWith(root + path.sep);
        } catch {
            return false;
        }
    }

    async edit({ jid, id, text }) {
        if (!this.open || !isJid(jid) || !isMessageId(id) || typeof text !== 'string' || !text || text.length > 65536) return;
        const sent = await this.sock.sendMessage(jid, { text, edit: { remoteJid: jid, id, fromMe: true } });
        this.edits.remember(sent?.key?.id, id);
    }

    /** What WhatsApp tells a linked device about a contact or group. */
    async profile({ jid }) {
        if (!this.open || !isJid(jid)) return;
        const out = { evt: 'profile', jid };
        if (jid.endsWith('@g.us')) {
            try {
                const g = await this.sock.groupMetadata(jid);
                out.group = {
                    subject: g.subject ?? '', description: g.desc ?? '', owner: g.owner ?? '',
                    created: Number(g.creation ?? 0),
                    participants: (g.participants ?? []).map((p) => ({ jid: jidNormalizedUser(p.id), admin: Boolean(p.admin) })),
                };
            } catch { /* not a member any more */ }
        } else {
            try {
                const status = await this.sock.fetchStatus(jid);
                const entry = Array.isArray(status) ? status[0]?.status : status;
                if (entry?.status) out.about = String(entry.status);
            } catch { /* private */ }
            try {
                const b = await this.sock.getBusinessProfile(jid);
                if (b) out.business = { address: b.address ?? '', email: b.email ?? '', category: b.category ?? '' };
            } catch { /* not a business */ }
        }
        emit(out);
    }

    /** Downloads a profile picture (preview or full) into the media folder. */
    async picture({ jid, full }) {
        if (!this.open || !isJid(jid)) return;
        let url = null;
        try { url = await this.sock.profilePictureUrl(jid, full ? 'image' : 'preview'); } catch { url = null; }
        if (!url || !String(url).startsWith('https://')) {
            emit({ evt: 'picture', jid, full: Boolean(full), none: true });
            return;
        }
        const id = createHash('sha256').update(String(url)).digest('hex').slice(0, 16);
        const name = `pic-${createHash('sha256').update(jid).digest('hex').slice(0, 16)}-${id}${full ? '-full' : ''}.jpg`;
        const file = path.join(this.mediaDir, name);
        try {
            await fs.access(file);
        } catch {
            const res = await fetch(url);
            if (!res.ok) return;
            const buf = Buffer.from(await res.arrayBuffer());
            if (buf.length > 4 * 1024 * 1024) return;
            await fs.writeFile(file + '.part', buf, { mode: 0o600 });
            await fs.rename(file + '.part', file);
        }
        emit({ evt: 'picture', jid, full: Boolean(full), path: file, id });
    }

    /** Declines an incoming call (tawk cannot carry call audio). */
    async rejectCall({ jid, id }) {
        if (!this.open || !isJid(jid) || !isMessageId(id)) return;
        await this.sock.rejectCall(id, jid);
    }

    /** Blocks or unblocks a contact, then publishes the block list. */
    /** The linked account's phone-number JID, or '' before linking. */
    ownJid() {
        return this.sock?.user?.id ? jidNormalizedUser(this.sock.user.id) : '';
    }

    /** Runs one change to your own profile and reports how it went. */
    async updateOwnProfile(field, change, extra = {}) {
        if (!this.open) {
            emit({ evt: 'profile_updated', field, ok: false, detail: 'not connected to WhatsApp' });
            return false;
        }
        try {
            await change();
        } catch (error) {
            emit({ evt: 'profile_updated', field, ok: false, detail: error?.message ?? 'WhatsApp refused the change' });
            return false;
        }
        emit({ evt: 'profile_updated', field, ok: true, ...extra });
        return true;
    }

    async setName({ name }) {
        const value = typeof name === 'string' ? name : '';
        await this.updateOwnProfile('name', () => this.sock.updateProfileName(value), { name: value });
    }

    async setAbout({ text }) {
        const value = typeof text === 'string' ? text : '';
        if (await this.updateOwnProfile('about', () => this.sock.updateProfileStatus(value))) {
            await this.profile({ jid: this.ownJid() }).catch(() => {});
        }
    }

    async setPicture({ path: file }) {
        if (typeof file !== 'string' || !(await this.insideMediaDir(file))) {
            emit({ evt: 'profile_updated', field: 'picture', ok: false, detail: 'the file cannot be sent' });
            return;
        }
        if (await this.updateOwnProfile('picture', () => this.sock.updateProfilePicture(this.ownJid(), { url: file }))) {
            emit({ evt: 'picture_changed', jid: this.ownJid() });
        }
    }

    async removePicture() {
        if (await this.updateOwnProfile('picture', () => this.sock.removeProfilePicture(this.ownJid()))) {
            emit({ evt: 'picture_changed', jid: this.ownJid() });
        }
    }

    async block({ jid, block }) {
        if (!this.open || !isJid(jid) || jid.endsWith('@g.us')) return;
        await this.sock.updateBlockStatus(jid, block ? 'block' : 'unblock');
        await this.publishBlocklist();
    }

    async publishBlocklist() {
        if (!this.open) return;
        try {
            const list = await this.sock.fetchBlocklist();
            emit({ evt: 'blocklist', jids: (list ?? []).map((j) => jidNormalizedUser(j)) });
        } catch { /* try again on the next change */ }
    }

    /** Deletes a whole chat on every device, like WhatsApp's "Delete chat". */
    async removeChat({ jid, id, from_me: fromMe, ts }) {
        if (!this.open || !isJid(jid)) return;
        const lastMessages = isMessageId(id) ? [{ key: { remoteJid: jid, id, fromMe: Boolean(fromMe) }, messageTimestamp: Number(ts) || 0 }] : [];
        await this.sock.chatModify({ delete: true, lastMessages }, jid);
    }

    /** Deletes a message for everyone (revoke) or only for this account. */
    async remove({ jid, id, sender, from_me: fromMe, everyone, ts }) {
        if (!this.open || !isJid(jid) || !isMessageId(id)) return;
        const key = { remoteJid: jid, id, fromMe: Boolean(fromMe) };
        if (jid.endsWith('@g.us') && sender && !fromMe) key.participant = sender;
        if (everyone) {
            await this.sock.sendMessage(jid, { delete: key });
            return;
        }
        await this.sock.chatModify({ deleteForMe: { deleteMedia: false, key, timestamp: Number(ts) || 0 } }, jid);
    }

    async react({ jid, id, sender, from_me: fromMe, emoji }) {
        if (!this.open || !isJid(jid) || !isMessageId(id) || String(emoji ?? '').length > 32) return;
        const key = { remoteJid: jid, id, fromMe: Boolean(fromMe) };
        if (jid.endsWith('@g.us') && sender && !fromMe) key.participant = sender;
        await this.sock.sendMessage(jid, { react: { text: emoji ?? '', key } });
    }

    async typing({ jid, state }) {
        if (!this.open || !isJid(jid)) return;
        const presence = state === 'composing' ? 'composing' : state === 'recording' ? 'recording' : 'paused';
        await this.sock.sendPresenceUpdate(presence, jid);
    }

    async subscribe({ jid }) {
        if (this.open && isJid(jid) && jid.endsWith('@s.whatsapp.net')) await this.sock.presenceSubscribe(jid);
    }

    async presence({ available }) {
        if (this.open) await this.sock.sendPresenceUpdate(available ? 'available' : 'unavailable');
    }

    async history({ jid, id, ts, from_me: fromMe, count }) {
        if (!this.open || !isJid(jid) || !isMessageId(id)) return;
        const n = Math.min(Math.max(Number(count) || 50, 1), 100);
        // The phone files a one-to-one chat under the phone number or the LID
        // and only answers for the form it uses, so ask with both.
        const chats = [jid, this.alternate.get(jid)].filter(Boolean);
        for (const remoteJid of chats) {
            await this.sock.fetchMessageHistory(n, { remoteJid, id, fromMe: Boolean(fromMe) }, Number(ts) || 0);
        }
    }

    /** Marks a chat read: read receipts (only when the user shares them) for
     * the listed messages and any seen arriving, then always the read mark
     * that clears the chat's unread badge on the phone and other devices. */
    async markRead({ jid, receipts, messages, last }) {
        if (!isJid(jid)) return;
        const chats = [jid, this.alternate.get(jid)].filter(Boolean);
        const seen = chats.flatMap((chat) => { const keys = this.unread.get(chat) ?? []; this.unread.delete(chat); return keys; });
        if (!this.open) return;
        if (receipts) {
            const group = jid.endsWith('@g.us');
            const keys = [...seen];
            const ids = new Set(keys.map((k) => k.id));
            for (const item of messages ?? []) {
                if (!isMessageId(item?.id) || ids.has(item.id)) continue;
                ids.add(item.id);
                keys.push({ remoteJid: jid, id: item.id, fromMe: false, ...(group && item.sender ? { participant: item.sender } : {}) });
            }
            if (keys.length) await this.sock.readMessages(keys);
        }
        if (last && isMessageId(last.id)) {
            const key = { remoteJid: jid, id: last.id, fromMe: Boolean(last.from_me),
                          ...(jid.endsWith('@g.us') && !last.from_me && last.sender ? { participant: last.sender } : {}) };
            await this.sock.chatModify({ markRead: true, lastMessages: [{ key, messageTimestamp: Number(last.ts) || 0 }] }, jid);
        }
    }

    async download(cmd) {
        try {
            if (!this.open) throw new Error('Not connected; media will download once WhatsApp is back.');
            const path = await this.downloader.download(this.sock, cmd);
            emit({ evt: 'media', id: cmd.id, path });
        } catch (error) {
            emit({ evt: 'error', id: cmd.id, detail: error.message });
        }
    }

    async logout() {
        try {
            await this.sock?.logout();
        } finally {
            this.sock = null;
            this.open = false;
            await fs.rm(this.authDir, { recursive: true, force: true });
            emit({ evt: 'logged_out' });
        }
    }
}
