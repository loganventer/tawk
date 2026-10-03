package main

import (
	"context"
	"time"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/types"
)

// sendMedia uploads a dropped file and sends it as a photo, video, audio file or document.
func (s *Session) sendMedia(cmd Command) {
	fail := func() { s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "failed"}) }
	if s.client == nil || !s.client.IsLoggedIn() || !isSafeID(cmd.ID) || !s.insideMediaDir(cmd.Path) {
		fail()
		return
	}
	jid, err := types.ParseJID(cmd.JID)
	if err != nil {
		fail()
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 5*time.Minute)
	defer cancel()
	msg, err := s.buildMediaMessage(ctx, cmd.Kind, cmd.Path, cmd.Mime, cmd.Name, cmd.Text)
	if err != nil {
		fail()
		return
	}
	if cmd.Forwarded {
		markForwarded(msg, cmd.ForwardingScore)
	}
	if _, err := s.client.SendMessage(ctx, jid, msg, whatsmeow.SendRequestExtra{ID: types.MessageID(cmd.ID)}); err != nil {
		fail()
		return
	}
	s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "sent"})
}
