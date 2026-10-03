package main

import (
	"context"
	"database/sql"
	"encoding/base64"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/rs/zerolog"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/store/sqlstore"
	"go.mau.fi/whatsmeow/types"
	waLog "go.mau.fi/whatsmeow/util/log"
	"google.golang.org/protobuf/proto"
	"modernc.org/sqlite"
)

func init() {
	// whatsmeow expects the "sqlite3" driver name; register the pure-Go
	// driver under it so no second copy of libsqlite3 is linked into tawk.
	sql.Register("sqlite3", &sqlite.Driver{})
}

// Session owns the whatsmeow client and serialises every command on one goroutine.
type Session struct {
	// id names this session to the C side: every event it sends carries it.
	id        int
	cfg       Config
	container *sqlstore.Container
	client    *whatsmeow.Client
	log       waLog.Logger
	commands  chan Command
	ctx       context.Context
	cancel    context.CancelFunc
	wg        sync.WaitGroup
	unread    *UnreadTracker
	edits     *EditReceipts
	qrOpen    atomic.Bool
	// dropped is set when keep-alives stop being answered and the socket is
	// closed on purpose, so a half-dead socket never counts as connected.
	dropped atomic.Bool
	// connCancel ends the context the current socket lives in.
	connCancel context.CancelFunc
}

// connectTimeout bounds a single dial and handshake, so a network that
// swallows packets cannot hold the command loop.
const connectTimeout = 30 * time.Second

// NewSession opens the login store and starts the command loop.
func NewSession(handle int, cfg Config) (*Session, error) {
	if err := os.MkdirAll(cfg.AuthDir, 0o700); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(cfg.MediaDir, 0o700); err != nil {
		return nil, err
	}
	if cfg.LogDir != "" {
		_ = os.MkdirAll(cfg.LogDir, 0o700)
	}
	// Never log to stdout or stderr: they belong to the terminal UI.
	log := waLog.Noop
	if cfg.Debug {
		path := filepath.Join(cfg.LogDir, logName(handle))
		if f, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o600); err == nil {
			log = waLog.Zerolog(zerolog.New(f).With().Timestamp().Logger().Level(zerolog.InfoLevel))
		}
	}
	dbPath := filepath.Join(cfg.AuthDir, "whatsmeow.db")
	if f, err := os.OpenFile(dbPath, os.O_CREATE|os.O_RDWR, 0o600); err == nil {
		f.Close()
	}
	ctx, cancel := context.WithCancel(context.Background())
	dsn := "file:" + dbPath + "?_pragma=foreign_keys(1)&_pragma=busy_timeout(5000)&_pragma=journal_mode(WAL)"
	container, err := sqlstore.New(ctx, "sqlite3", dsn, log)
	if err != nil {
		cancel()
		return nil, err
	}
	s := &Session{
		id:        handle,
		cfg:       cfg,
		container: container,
		log:       log,
		commands:  make(chan Command, 256),
		ctx:       ctx,
		cancel:    cancel,
		unread:    NewUnreadTracker(),
		edits:     NewEditReceipts(),
	}
	s.wg.Add(1)
	go s.loop()
	return s, nil
}

// logName gives each session its own debug log. The first keeps the name it
// always had.
func logName(handle int) string {
	if handle <= 1 {
		return "whatsmeow.log"
	}
	return fmt.Sprintf("whatsmeow-%d.log", handle)
}

// emit sends one protocol event to the C side, marked as this session's.
func (s *Session) emit(event map[string]any) {
	emitTo(s.id, event)
}

// Enqueue hands a command to the session loop without blocking the caller.
func (s *Session) Enqueue(cmd Command) bool {
	select {
	case s.commands <- cmd:
		return true
	default:
		return false
	}
}

// Close disconnects and stops the loop.
func (s *Session) Close() {
	s.cancel()
	s.wg.Wait()
	if s.client != nil {
		s.client.Disconnect()
	}
	s.container.Close()
}

func (s *Session) loop() {
	defer s.wg.Done()
	for {
		select {
		case <-s.ctx.Done():
			return
		case cmd := <-s.commands:
			s.handle(cmd)
		}
	}
}

func (s *Session) handle(cmd Command) {
	defer func() {
		if r := recover(); r != nil {
			s.emit(map[string]any{"evt": "error", "detail": fmt.Sprintf("Internal error handling %s", cmd.Cmd)})
		}
	}()
	switch cmd.Cmd {
	case "connect":
		s.connect(false)
	case "reconnect":
		s.reconnect()
	case "qr":
		s.connect(true)
	case "pair":
		s.pair(cmd.Phone)
	case "send":
		s.send(cmd)
	case "send_voice":
		go s.sendVoice(cmd)
	case "send_media":
		go s.sendMedia(cmd)
	case "forward_media":
		go s.forwardMedia(cmd)
	case "react":
		go s.react(cmd)
	case "edit":
		go s.editMessage(cmd)
	case "delete":
		go s.deleteMessage(cmd)
	case "delete_chat":
		go s.deleteChat(cmd)
	case "profile":
		go s.profile(cmd)
	case "set_name":
		go s.setName(cmd)
	case "set_about":
		go s.setAbout(cmd)
	case "set_picture":
		go s.setPicture(cmd)
	case "remove_picture":
		go s.removePicture()
	case "post_status":
		go s.postStatus(cmd)
	case "picture":
		go s.picture(cmd)
	case "block":
		go s.block(cmd)
	case "reject_call":
		go s.rejectCall(cmd)
	case "typing":
		s.typing(cmd)
	case "subscribe":
		s.subscribe(cmd)
	case "presence":
		s.presence(cmd)
	case "history":
		go s.history(cmd)
	case "download":
		go s.download(cmd)
	case "read":
		go s.markRead(cmd)
	case "logout":
		s.logout()
	}
}

// ensureClient creates a client bound to the stored device (or a new one).
func (s *Session) ensureClient() error {
	if s.client != nil && !s.client.Store.Deleted {
		return nil
	}
	device, err := s.container.GetFirstDevice(s.ctx)
	if err != nil {
		return err
	}
	client := whatsmeow.NewClient(device, s.log)
	// Reconnection policy (exponential backoff, circuit breaker) lives in
	// the C messaging manager, so the library must not retry by itself.
	client.EnableAutoReconnect = false
	client.AddEventHandler(s.onEvent)
	s.client = client
	return nil
}

func (s *Session) connect(freshQR bool) {
	if err := s.ensureClient(); err != nil {
		s.emitClosed("error", "Could not load the login store: "+err.Error())
		return
	}
	stale := s.dropped.Load() || (s.client.Store.ID != nil && !s.client.IsLoggedIn())
	if s.client.IsConnected() && (freshQR || stale) {
		s.client.Disconnect()
	}
	if s.client.IsConnected() {
		if s.client.IsLoggedIn() {
			s.emit(map[string]any{"evt": "connection", "reason": "open", "detail": "Connected"})
		}
		return
	}
	if s.client.Store.ID == nil {
		s.emit(map[string]any{"evt": "auth_required"})
		s.startQR()
	}
	s.emit(map[string]any{"evt": "connection", "reason": "connecting", "detail": "Connecting to WhatsApp"})
	// The socket lives in the context it was dialled with, so the deadline
	// cancels it only while the dial and handshake are still running.
	if s.connCancel != nil {
		s.connCancel()
	}
	ctx, cancel := context.WithCancel(s.ctx)
	s.connCancel = cancel
	timer := time.AfterFunc(connectTimeout, cancel)
	err := s.client.ConnectContext(ctx)
	if !timer.Stop() && err == nil {
		err = context.DeadlineExceeded
	}
	if err != nil {
		cancel()
		s.emitClosed("error", "Could not reach WhatsApp: "+err.Error())
		return
	}
	s.dropped.Store(false)
}

// reconnect drops the current socket, working or not, and dials again. The
// C side asks for it when the machine's network changes, since a socket
// bound to the old adapter usually hangs without ever reporting an error.
func (s *Session) reconnect() {
	if s.client != nil && s.client.IsConnected() {
		s.client.Disconnect()
	}
	s.connect(false)
}

// startQR opens a QR channel unless one is still open. An open channel stays
// subscribed to the client across reconnects and emits the new socket's codes.
// Cancelling it instead would make it disconnect the new socket as soon as
// WhatsApp sends those codes.
func (s *Session) startQR() {
	if s.qrOpen.Load() {
		return
	}
	qrChan, err := s.client.GetQRChannel(s.ctx)
	if err != nil {
		return
	}
	s.qrOpen.Store(true)
	go func() {
		defer s.qrOpen.Store(false)
		for item := range qrChan {
			switch item.Event {
			case "code":
				s.emit(map[string]any{"evt": "qr", "ascii": renderQR(item.Code)})
			case "timeout":
				s.emitClosed("qr_timeout", "The QR code expired. Press R for a new one.")
			case "success":
				return
			default:
				if item.Error != nil {
					s.emit(map[string]any{"evt": "error", "detail": "Pairing failed: " + item.Error.Error()})
				}
			}
		}
	}()
}

func (s *Session) pair(phone string) {
	digits := onlyDigits(phone)
	if len(digits) < 8 || len(digits) > 15 {
		s.emit(map[string]any{"evt": "error", "detail": "Enter the full number with country code, e.g. 27821234567."})
		return
	}
	// WhatsApp closes the login websocket 160 seconds after the QR session
	// starts, which may be long before the user has typed a number. Pair on a
	// fresh socket so the code gets the full time, and retry once if the socket
	// drops before WhatsApp answers.
	var code string
	var err error
	for attempt := 0; attempt < 2; attempt++ {
		if err = s.freshLoginSocket(); err != nil {
			break
		}
		code, err = s.client.PairPhone(s.ctx, digits, true, whatsmeow.PairClientChrome, "Chrome (Linux)")
		if !errors.Is(err, whatsmeow.ErrNotConnected) {
			break
		}
	}
	if err != nil {
		s.emit(map[string]any{"evt": "error", "detail": "Could not get a pairing code: " + err.Error()})
		return
	}
	s.emit(map[string]any{"evt": "pairing_code", "code": code})
}

// freshLoginSocket reconnects with a new QR session and waits until the
// socket is up, as PairPhone needs.
func (s *Session) freshLoginSocket() error {
	s.connect(true)
	if s.client == nil || !s.client.IsConnected() {
		return errors.New("could not reach WhatsApp")
	}
	time.Sleep(time.Second) // whatsmeow asks for a moment after Connect before PairPhone
	if !s.client.IsConnected() {
		return whatsmeow.ErrNotConnected
	}
	return nil
}

func (s *Session) send(cmd Command) {
	if s.client == nil || !s.client.IsLoggedIn() {
		s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "failed"})
		return
	}
	jid, err := types.ParseJID(cmd.JID)
	if err != nil || len(cmd.Text) == 0 || len(cmd.Text) > 65536 {
		s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "failed"})
		return
	}
	text := cmd.Text
	var ctxInfo *waE2E.ContextInfo
	if cmd.ReplyTo != nil && isSafeID(cmd.ReplyTo.ID) {
		ctxInfo = &waE2E.ContextInfo{
			StanzaID:      proto.String(cmd.ReplyTo.ID),
			QuotedMessage: &waE2E.Message{Conversation: proto.String(cmd.ReplyTo.Text)},
		}
		if cmd.ReplyTo.Sender != "" {
			ctxInfo.Participant = proto.String(cmd.ReplyTo.Sender)
		}
		if cmd.ReplyTo.Status { // a reply to someone's status
			ctxInfo.RemoteJID = proto.String(types.StatusBroadcastJID.String())
		}
	}
	if len(cmd.Mentions) > 0 {
		var jids []string
		text, jids = s.mentionTargets(jid, text, cmd.Mentions)
		if ctxInfo == nil {
			ctxInfo = &waE2E.ContextInfo{}
		}
		ctxInfo.MentionedJID = jids
	}
	var preview *LinkPreview
	if cmd.LinkPreview {
		if link := firstURL.FindString(text); link != "" {
			preview, _ = fetchLinkPreview(s.ctx, strings.TrimRight(link, ".,;:!?)]'\""))
		}
	}
	msg := &waE2E.Message{Conversation: proto.String(text)}
	if ctxInfo != nil || preview != nil {
		ext := &waE2E.ExtendedTextMessage{Text: proto.String(text), ContextInfo: ctxInfo}
		if preview != nil {
			ext.MatchedText = proto.String(preview.URL)
			ext.Title = proto.String(preview.Title)
			ext.Description = proto.String(preview.Description)
			if len(preview.Thumb) > 0 {
				ext.JPEGThumbnail = preview.Thumb
			}
			ext.PreviewType = waE2E.ExtendedTextMessage_NONE.Enum()
		}
		msg = &waE2E.Message{ExtendedTextMessage: ext}
	}
	if cmd.Forwarded {
		markForwarded(msg, cmd.ForwardingScore)
	}
	extra := whatsmeow.SendRequestExtra{}
	if isSafeID(cmd.ID) {
		extra.ID = types.MessageID(cmd.ID)
	}
	if _, err := s.client.SendMessage(s.ctx, jid, msg, extra); err != nil {
		s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "failed"})
		return
	}
	s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "sent"})
	if preview != nil {
		link := map[string]any{"evt": "link", "id": cmd.ID, "url": preview.URL, "title": preview.Title, "desc": preview.Description}
		if len(preview.Thumb) > 0 {
			link["thumb"] = base64.StdEncoding.EncodeToString(preview.Thumb)
		}
		s.emit(link)
	}
}

func (s *Session) logout() {
	if s.client == nil {
		return
	}
	if err := s.client.Logout(s.ctx); err != nil {
		s.client.Disconnect()
		_ = s.client.Store.Delete(s.ctx)
	}
	s.client = nil
	s.emit(map[string]any{"evt": "logged_out"})
}

func (s *Session) emitClosed(reason, detail string) {
	s.emit(map[string]any{"evt": "connection", "reason": reason, "detail": detail})
}

func onlyDigits(s string) string {
	var b strings.Builder
	for _, r := range s {
		if r >= '0' && r <= '9' {
			b.WriteRune(r)
		}
	}
	return b.String()
}

func isSafeID(id string) bool {
	if len(id) == 0 || len(id) > 64 {
		return false
	}
	for _, r := range id {
		if !(r >= '0' && r <= '9' || r >= 'A' && r <= 'Z' || r >= 'a' && r <= 'z') {
			return false
		}
	}
	return true
}
