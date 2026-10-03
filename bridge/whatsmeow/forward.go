package main

import (
	"context"
	"time"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/types"
	"google.golang.org/protobuf/proto"
)

// forwardMedia sends media WhatsApp already holds (a received message's
// download reference) to another chat, marked as forwarded, without
// downloading or uploading it.
func (s *Session) forwardMedia(cmd Command) {
	fail := func() { s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "failed"}) }
	if s.client == nil || !s.client.IsLoggedIn() || !isSafeID(cmd.ID) {
		fail()
		return
	}
	jid, err := types.ParseJID(cmd.JID)
	if err != nil {
		fail()
		return
	}
	media, _, err := decodeRef(cmd.Ref)
	if err != nil {
		fail()
		return
	}
	msg := &waE2E.Message{}
	switch m := media.(type) {
	case *waE2E.ImageMessage:
		msg.ImageMessage = m
	case *waE2E.VideoMessage:
		msg.VideoMessage = m
	case *waE2E.AudioMessage:
		msg.AudioMessage = m
	case *waE2E.DocumentMessage:
		msg.DocumentMessage = m
	case *waE2E.StickerMessage:
		msg.StickerMessage = m
	default:
		fail()
		return
	}
	markForwarded(msg, cmd.ForwardingScore)
	ctx, cancel := context.WithTimeout(s.ctx, 2*time.Minute)
	defer cancel()
	if _, err := s.client.SendMessage(ctx, jid, msg, whatsmeow.SendRequestExtra{ID: types.MessageID(cmd.ID)}); err != nil {
		fail()
		return
	}
	s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "sent"})
}

// markForwarded sets the "Forwarded" mark on whatever the message carries,
// replacing any context it came with (a forwarded message answers nothing).
// Plain text becomes extended text, which is the kind that has a context.
func markForwarded(msg *waE2E.Message, score uint32) {
	if score == 0 {
		score = 1
	}
	info := func(old *waE2E.ContextInfo) *waE2E.ContextInfo {
		ctx := &waE2E.ContextInfo{IsForwarded: proto.Bool(true), ForwardingScore: proto.Uint32(score)}
		if old != nil {
			ctx.MentionedJID = old.MentionedJID
		}
		return ctx
	}
	if msg.Conversation != nil {
		msg.ExtendedTextMessage = &waE2E.ExtendedTextMessage{Text: msg.Conversation}
		msg.Conversation = nil
	}
	switch {
	case msg.ExtendedTextMessage != nil:
		msg.ExtendedTextMessage.ContextInfo = info(msg.ExtendedTextMessage.ContextInfo)
	case msg.ImageMessage != nil:
		msg.ImageMessage.ContextInfo = info(msg.ImageMessage.ContextInfo)
	case msg.VideoMessage != nil:
		msg.VideoMessage.ContextInfo = info(msg.VideoMessage.ContextInfo)
	case msg.AudioMessage != nil:
		msg.AudioMessage.ContextInfo = info(msg.AudioMessage.ContextInfo)
	case msg.DocumentMessage != nil:
		msg.DocumentMessage.ContextInfo = info(msg.DocumentMessage.ContextInfo)
	case msg.StickerMessage != nil:
		msg.StickerMessage.ContextInfo = info(msg.StickerMessage.ContextInfo)
	}
}

// forwardedOf reports whether a received message carries the "Forwarded" mark.
func forwardedOf(m *waE2E.Message) bool {
	if m == nil {
		return false
	}
	for _, ctx := range []*waE2E.ContextInfo{
		m.GetExtendedTextMessage().GetContextInfo(), m.GetImageMessage().GetContextInfo(),
		m.GetVideoMessage().GetContextInfo(), m.GetAudioMessage().GetContextInfo(),
		m.GetDocumentMessage().GetContextInfo(), m.GetStickerMessage().GetContextInfo(),
	} {
		if ctx.GetIsForwarded() {
			return true
		}
	}
	return false
}
