package main

import (
	"context"
	"encoding/base64"
	"time"

	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
)

// onEvent translates whatsmeow events into protocol events.
func (s *Session) onEvent(raw any) {
	switch evt := raw.(type) {
	case *events.Message:
		if pm := evt.Message.GetProtocolMessage(); pm != nil && pm.GetKey().GetID() != "" {
			switch pm.GetType() {
			case waE2E.ProtocolMessage_MESSAGE_EDIT:
				s.emit(map[string]any{"evt": "edit", "id": pm.GetKey().GetID(), "chat": s.phoneJID(evt.Info.Chat),
					"text": describe(pm.GetEditedMessage()).text})
			case waE2E.ProtocolMessage_REVOKE:
				s.emit(map[string]any{"evt": "edit", "id": pm.GetKey().GetID(), "chat": s.phoneJID(evt.Info.Chat), "deleted": true})
			}
			return
		}
		if r := evt.Message.GetReactionMessage(); r != nil {
			s.emit(map[string]any{"evt": "reaction", "id": r.GetKey().GetID(), "chat": s.phoneJID(evt.Info.Chat),
				"sender": s.phoneJID(evt.Info.Sender), "emoji": r.GetText()})
			return
		}
		s.emitMessage(evt, true)
	case *events.ChatPresence:
		state := "paused"
		if evt.State == types.ChatPresenceComposing {
			state = "composing"
			if evt.Media == types.ChatPresenceMediaAudio {
				state = "recording"
			}
		}
		s.emit(map[string]any{"evt": "typing", "chat": s.phoneJID(evt.Chat), "sender": s.phoneJID(evt.Sender), "state": state})
	case *events.Presence:
		state := "online"
		if evt.Unavailable {
			state = "offline"
		}
		lastSeen := int64(0)
		if !evt.LastSeen.IsZero() {
			lastSeen = evt.LastSeen.Unix()
		}
		s.emit(map[string]any{"evt": "presence", "jid": s.phoneJID(evt.From), "state": state, "last_seen": lastSeen})
	case *events.CallOffer:
		s.emitCallOffer(evt)
	case *events.CallOfferNotice:
		s.emitCall(evt.BasicCallMeta, "offer", evt.Media == "video", evt.Type == "group")
	case *events.CallAccept:
		s.emitCall(evt.BasicCallMeta, "accepted", false, false)
	case *events.CallTerminate:
		s.emitCall(evt.BasicCallMeta, "ended", false, false)
	case *events.Blocklist:
		go s.publishBlocklist()
	case *events.Picture:
		s.emit(map[string]any{"evt": "picture_changed", "jid": s.phoneJID(evt.JID)})
	case *events.DeleteChat:
		s.emit(map[string]any{"evt": "chat_removed", "jid": s.phoneJID(evt.JID)})
	case *events.DeleteForMe:
		if isSafeID(evt.MessageID) {
			s.emit(map[string]any{"evt": "removed", "id": evt.MessageID, "chat": s.phoneJID(evt.ChatJID)})
		}
	case *events.Archive:
		if evt.Action != nil {
			s.emit(map[string]any{"evt": "chat", "jid": s.phoneJID(evt.JID), "unread": -1, "archived": evt.Action.GetArchived()})
		}
	case *events.HistorySync:
		go s.emitHistory(evt)
	case *events.Receipt:
		status := ""
		switch evt.Type {
		case types.ReceiptTypeDelivered:
			status = "delivered"
		case types.ReceiptTypeRead, types.ReceiptTypeReadSelf:
			status = "read"
		}
		if status != "" && evt.IsFromMe == false {
			for _, id := range evt.MessageIDs {
				s.emit(map[string]any{"evt": "status", "id": string(s.edits.Resolve(id)), "status": status})
			}
		}
		// Who got how far with each message, for the message info panel.
		kind := ""
		switch evt.Type {
		case types.ReceiptTypeDelivered:
			kind = "delivered"
		case types.ReceiptTypeRead:
			kind = "read"
		case types.ReceiptTypePlayed:
			kind = "played"
		}
		if kind != "" && !evt.IsFromMe {
			by := s.phoneJID(evt.Sender.ToNonAD())
			for _, id := range evt.MessageIDs {
				s.emit(map[string]any{"evt": "receipt", "id": string(s.edits.Resolve(id)), "by": by, "kind": kind, "at": evt.Timestamp.Unix()})
			}
		}
	case *events.Connected:
		s.dropped.Store(false)
		name := ""
		jid := ""
		if s.client.Store.ID != nil {
			jid = s.client.Store.ID.ToNonAD().String()
			name = s.client.Store.PushName
		}
		s.emit(map[string]any{"evt": "connected", "jid": jid, "name": name})
		s.emit(map[string]any{"evt": "connection", "reason": "open", "detail": "Connected"})
		go func() {
			s.emitStoredAliases()
			s.syncDirectory()
			s.publishBlocklist()
		}()
	case *events.Disconnected:
		s.emitClosed("closed", "The connection to WhatsApp dropped.")
	case *events.KeepAliveTimeout:
		// With auto-reconnect off, whatsmeow keeps a socket whose pings go
		// unanswered open forever (typically after a network change), so
		// close it here once and let the C side dial again.
		if evt.ErrorCount >= 3 && s.dropped.CompareAndSwap(false, true) {
			go s.client.Disconnect()
			s.emitClosed("closed", "WhatsApp stopped answering keep-alive pings.")
		}
	case *events.KeepAliveRestored:
		if !s.dropped.Load() {
			s.emit(map[string]any{"evt": "connection", "reason": "open", "detail": "Connected"})
		}
	case *events.StreamReplaced:
		s.emitClosed("replaced", "Another WhatsApp Web session took over this login.")
	case *events.LoggedOut:
		s.client = nil
		s.emit(map[string]any{"evt": "logged_out"})
	case *events.TemporaryBan:
		s.emitClosed("banned", "WhatsApp temporarily blocked this account: "+evt.String())
	case *events.ClientOutdated:
		s.emitClosed("outdated", "WhatsApp rejected this client version; update tawk.")
	case *events.ConnectFailure:
		s.emitClosed("error", "WhatsApp refused the connection: "+evt.Reason.String())
	case *events.PushName:
		s.emit(map[string]any{"evt": "contact", "jid": s.phoneJID(evt.JID), "push_name": evt.NewPushName})
	case *events.Contact:
		if evt.Action != nil {
			s.emit(map[string]any{"evt": "contact", "jid": s.phoneJID(evt.JID), "name": evt.Action.GetFullName()})
		}
	case *events.GroupInfo:
		if evt.Name != nil {
			s.emit(map[string]any{"evt": "chat", "jid": evt.JID.String(), "name": evt.Name.Name, "unread": -1})
		}
	}
}

// phoneJID maps a hidden-user (LID) JID to its phone-number JID when known,
// so chats and contacts line up regardless of addressing mode.
func (s *Session) phoneJID(jid types.JID) string {
	if jid.Server == types.HiddenUserServer && s.client != nil {
		if pn, err := s.client.Store.LIDs.GetPNForLID(context.Background(), jid); err == nil && !pn.IsEmpty() {
			return pn.ToNonAD().String()
		}
	}
	return jid.ToNonAD().String()
}

func (s *Session) emitMessage(evt *events.Message, live bool) {
	content := describe(evt.Message)
	if content.kind == "" {
		return
	}
	// Prefer the phone-number form the message itself carries for LID chats.
	info := evt.Info
	if info.Chat.Server == types.HiddenUserServer && !info.IsGroup {
		alt := info.SenderAlt
		if info.IsFromMe {
			alt = info.RecipientAlt
		}
		s.rememberAlias(info.Chat, alt)
	}
	if info.Sender.Server == types.HiddenUserServer {
		s.rememberAlias(info.Sender, info.SenderAlt)
	}
	chat := s.phoneJID(evt.Info.Chat)
	sender := s.phoneJID(evt.Info.Sender)
	out := map[string]any{
		"evt":         "message",
		"id":          string(evt.Info.ID),
		"chat":        chat,
		"sender":      sender,
		"sender_name": evt.Info.PushName,
		"text":        content.text,
		"type":        content.kind,
		"ts":          evt.Info.Timestamp.Unix(),
		"from_me":     evt.Info.IsFromMe,
		"live":        live,
		"status":      "delivered",
	}
	if evt.Info.IsFromMe {
		out["status"] = "sent"
	}
	if content.ref != "" {
		out["ref"] = content.ref
	}
	if forwardedOf(evt.Message) {
		out["forwarded"] = true
	}
	if content.seconds > 0 {
		out["seconds"] = content.seconds
	}
	if content.bg != 0 {
		out["bg"] = content.bg
	}
	if content.link != nil {
		out["link"] = content.link
	}
	if len(content.thumb) > 0 && len(content.thumb) < 48*1024 {
		out["thumb"] = base64.StdEncoding.EncodeToString(content.thumb)
	}
	if q := quoteOf(evt.Message); q != nil {
		if q.Sender != "" {
			if jid, err := types.ParseJID(q.Sender); err == nil {
				q.Sender = s.phoneJID(jid)
			}
		}
		out["quote"] = q
	}
	if mentions, me := s.mentionsOf(evt.Message); len(mentions) > 0 {
		out["mentions"] = mentions
		out["mentions_me"] = me
	}
	s.emit(out)
	if live && !evt.Info.IsFromMe {
		s.unread.Add(evt.Info.Chat.String(), evt.Info.Sender.String(), evt.Info.ID)
	}
}

func (s *Session) emitHistory(evt *events.HistorySync) {
	var pairs [][2]types.JID
	for _, m := range evt.Data.GetPhoneNumberToLidMappings() {
		lid, err1 := types.ParseJID(m.GetLidJID())
		pn, err2 := types.ParseJID(m.GetPnJID())
		if err1 == nil && err2 == nil {
			pairs = append(pairs, [2]types.JID{lid, pn})
		}
	}
	for _, conv := range evt.Data.GetConversations() {
		lid, err1 := types.ParseJID(conv.GetLidJID())
		pn, err2 := types.ParseJID(conv.GetPnJID())
		if err1 == nil && err2 == nil && conv.GetLidJID() != "" && conv.GetPnJID() != "" {
			pairs = append(pairs, [2]types.JID{lid, pn})
		}
	}
	s.storeHistoryMappings(pairs)
	for _, conv := range evt.Data.GetConversations() {
		jid, err := types.ParseJID(conv.GetID())
		if err != nil {
			continue
		}
		s.emit(map[string]any{
			"evt":      "chat",
			"jid":      s.phoneJID(jid),
			"name":     conv.GetName(),
			"unread":   int(conv.GetUnreadCount()),
			"ts":       int64(conv.GetConversationTimestamp()),
			"archived": conv.GetArchived(),
		})
		for _, item := range conv.GetMessages() {
			parsed, err := s.client.ParseWebMessage(jid, item.GetMessage())
			if err != nil {
				continue
			}
			s.emitMessage(parsed, false)
		}
	}
	// Statuses posted before this device was linked, or while it was away,
	// come in their own list; tawk keeps them as statuses.
	for _, item := range evt.Data.GetStatusV3Messages() {
		parsed, err := s.client.ParseWebMessage(types.StatusBroadcastJID, item)
		if err != nil {
			continue
		}
		s.emitMessage(parsed, false)
	}
}

// syncDirectory publishes group names and address-book contacts after connecting.
func (s *Session) syncDirectory() {
	ctx, cancel := context.WithTimeout(s.ctx, 60*time.Second)
	defer cancel()
	if s.client == nil {
		return
	}
	if groups, err := s.client.GetJoinedGroups(ctx); err == nil {
		for _, g := range groups {
			s.emit(map[string]any{"evt": "chat", "jid": g.JID.String(), "name": g.Name, "unread": -1})
		}
	}
	if contacts, err := s.client.Store.Contacts.GetAllContacts(ctx); err == nil {
		for jid, c := range contacts {
			s.emit(map[string]any{"evt": "contact", "jid": s.phoneJID(jid), "name": c.FullName, "push_name": c.PushName})
		}
	}
}

// messageContent is the displayable part of a message.
type messageContent struct {
	kind    string
	text    string
	ref     string
	seconds uint32
	thumb   []byte
	bg      uint32 // a text status's background colour (ARGB)
	link    *LinkSpec
}

// LinkSpec is the preview card of a web address in a message.
type LinkSpec struct {
	URL   string `json:"url"`
	Title string `json:"title"`
	Desc  string `json:"desc"`
}

// quoteOf returns the message a reply refers to, when there is one.
func quoteOf(m *waE2E.Message) *QuoteSpec {
	ci := contextInfoOf(m)
	if ci == nil || ci.GetStanzaID() == "" {
		return nil
	}
	text := describe(ci.GetQuotedMessage()).text
	if text == "" {
		text = describe(ci.GetQuotedMessage()).kind
	}
	if len(text) > 300 {
		text = text[:300]
	}
	return &QuoteSpec{ID: ci.GetStanzaID(), Sender: ci.GetParticipant(), Text: text,
		Status: ci.GetRemoteJID() == types.StatusBroadcastJID.String()}
}

func describe(m *waE2E.Message) messageContent {
	if m == nil {
		return messageContent{}
	}
	switch {
	case m.GetConversation() != "":
		return messageContent{kind: "text", text: m.GetConversation()}
	case m.GetExtendedTextMessage() != nil:
		ext := m.GetExtendedTextMessage()
		content := messageContent{kind: "text", text: ext.GetText(), bg: ext.GetBackgroundArgb()}
		if url := ext.GetMatchedText(); url != "" && (ext.GetTitle() != "" || ext.GetDescription() != "") {
			content.link = &LinkSpec{URL: url, Title: ext.GetTitle(), Desc: ext.GetDescription()}
			content.thumb = ext.GetJPEGThumbnail()
		}
		return content
	case m.GetImageMessage() != nil:
		img := m.GetImageMessage()
		return messageContent{kind: "image", text: img.GetCaption(), ref: encodeRef("image", img), thumb: img.GetJPEGThumbnail()}
	case m.GetVideoMessage() != nil:
		vid := m.GetVideoMessage()
		return messageContent{kind: "video", text: vid.GetCaption(), ref: encodeRef("video", vid), thumb: vid.GetJPEGThumbnail()}
	case m.GetAudioMessage() != nil:
		a := m.GetAudioMessage()
		return messageContent{kind: "audio", ref: encodeRef("audio", a), seconds: a.GetSeconds()}
	case m.GetDocumentMessage() != nil:
		d := m.GetDocumentMessage()
		text := d.GetFileName()
		if d.GetCaption() != "" {
			text += " " + d.GetCaption()
		}
		return messageContent{kind: "document", text: text, ref: encodeRef("document", d), thumb: d.GetJPEGThumbnail()}
	case m.GetStickerMessage() != nil:
		return messageContent{kind: "sticker", ref: encodeRef("sticker", m.GetStickerMessage())}
	case m.GetLocationMessage() != nil:
		l := m.GetLocationMessage()
		return messageContent{kind: "other", text: "📍 " + l.GetName() + " " + l.GetAddress()}
	case m.GetContactMessage() != nil:
		return messageContent{kind: "other", text: "👤 " + m.GetContactMessage().GetDisplayName()}
	case m.GetPollCreationMessage() != nil:
		return messageContent{kind: "other", text: "📊 " + m.GetPollCreationMessage().GetName()}
	}
	return messageContent{}
}
