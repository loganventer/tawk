package main

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/types"
	"google.golang.org/protobuf/proto"
)

const maxVoiceBytes = 16 * 1024 * 1024

// sendVoice uploads an Ogg/Opus recording and sends it as a push-to-talk voice note.
func (s *Session) sendVoice(cmd Command) {
	fail := func() { s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "failed"}) }
	if s.client == nil || !s.client.IsLoggedIn() || !isSafeID(cmd.ID) {
		fail()
		return
	}
	jid, err := types.ParseJID(cmd.JID)
	if err != nil || !s.insideMediaDir(cmd.Path) {
		fail()
		return
	}
	info, err := os.Stat(cmd.Path)
	if err != nil || !info.Mode().IsRegular() || info.Size() == 0 || info.Size() > maxVoiceBytes {
		fail()
		return
	}
	data, err := os.ReadFile(cmd.Path)
	if err != nil {
		fail()
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 2*time.Minute)
	defer cancel()
	up, err := s.client.Upload(ctx, data, whatsmeow.MediaAudio)
	if err != nil {
		fail()
		return
	}
	msg := &waE2E.Message{AudioMessage: &waE2E.AudioMessage{
		URL:           proto.String(up.URL),
		DirectPath:    proto.String(up.DirectPath),
		MediaKey:      up.MediaKey,
		FileEncSHA256: up.FileEncSHA256,
		FileSHA256:    up.FileSHA256,
		FileLength:    proto.Uint64(up.FileLength),
		Mimetype:      proto.String("audio/ogg; codecs=opus"),
		Seconds:       proto.Uint32(uint32(max(cmd.Secs, 1))),
		PTT:           proto.Bool(true),
	}}
	if _, err := s.client.SendMessage(ctx, jid, msg, whatsmeow.SendRequestExtra{ID: types.MessageID(cmd.ID)}); err != nil {
		fail()
		return
	}
	s.emit(map[string]any{"evt": "status", "id": cmd.ID, "status": "sent"})
}

// insideMediaDir rejects any path outside the configured media folder.
func (s *Session) insideMediaDir(path string) bool {
	root, err := filepath.EvalSymlinks(s.cfg.MediaDir)
	if err != nil {
		return false
	}
	real, err := filepath.EvalSymlinks(path)
	if err != nil {
		return false
	}
	return strings.HasPrefix(real, root+string(os.PathSeparator))
}
