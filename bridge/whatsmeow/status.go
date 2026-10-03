package main

import (
	"context"
	"regexp"
	"strings"
	"time"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/types"
	"google.golang.org/protobuf/proto"
)

// statusTextColour is the colour of the words on a text status.
const statusTextColour = 0xFFFFFFFF

var firstURL = regexp.MustCompile(`(?i)https?://[^\s<>"]+`)

// postStatus sends a status to status@broadcast. WhatsApp delivers it to the
// people the phone's status privacy setting allows; whatsmeow reads that list.
func (s *Session) postStatus(cmd Command) {
	done := func(err error) {
		out := map[string]any{"evt": "status_posted", "id": cmd.ID, "ok": err == nil}
		if err != nil {
			out["detail"] = err.Error()
		}
		s.emit(out)
	}
	if !isSafeID(cmd.ID) {
		return
	}
	if s.client == nil || !s.client.IsLoggedIn() {
		s.emit(map[string]any{"evt": "status_posted", "id": cmd.ID, "ok": false, "detail": "Not connected to WhatsApp."})
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 5*time.Minute)
	defer cancel()
	var msg *waE2E.Message
	switch cmd.Kind {
	case "text", "link":
		ext := &waE2E.ExtendedTextMessage{
			Text:           proto.String(cmd.Text),
			TextArgb:       proto.Uint32(statusTextColour),
			BackgroundArgb: proto.Uint32(cmd.BG),
			Font:           waE2E.ExtendedTextMessage_FontType(cmd.Font).Enum(),
		}
		if url := strings.TrimRight(firstURL.FindString(cmd.Text), ".,;:!?)]'\""); url != "" {
			ext.MatchedText = proto.String(url)
		}
		msg = &waE2E.Message{ExtendedTextMessage: ext}
	case "image", "video":
		built, err := s.buildMediaMessage(ctx, cmd.Kind, cmd.Path, cmd.Mime, "", cmd.Text)
		if err != nil {
			done(err)
			return
		}
		msg = built
	default:
		s.emit(map[string]any{"evt": "status_posted", "id": cmd.ID, "ok": false, "detail": "Unknown kind of status."})
		return
	}
	resp, err := s.client.SendMessage(ctx, types.StatusBroadcastJID, msg, whatsmeow.SendRequestExtra{ID: types.MessageID(cmd.ID)})
	done(err)
	if err == nil {
		s.echoStatus(cmd, msg, resp.Timestamp)
	}
}

// echoStatus reports a status you just posted as a message on
// status@broadcast, so it shows among your statuses like one posted from the
// phone. A photo or video keeps its local file (path) and a reference for
// downloading it again.
func (s *Session) echoStatus(cmd Command, msg *waE2E.Message, at time.Time) {
	own := s.ownJID()
	if own == "" {
		return
	}
	if at.IsZero() {
		at = time.Now()
	}
	content := describe(msg)
	out := map[string]any{
		"evt": "message", "id": cmd.ID, "chat": types.StatusBroadcastJID.String(), "sender": own,
		"sender_name": s.client.Store.PushName, "text": content.text, "type": content.kind,
		"ts": at.Unix(), "from_me": true, "live": true, "status": "sent",
	}
	if content.bg != 0 {
		out["bg"] = content.bg
	}
	if content.ref != "" {
		out["ref"] = content.ref
		out["path"] = cmd.Path
	}
	s.emit(out)
}
