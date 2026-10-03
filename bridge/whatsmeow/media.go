package main

import (
	"context"
	"encoding/base64"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"google.golang.org/protobuf/proto"
)

// mediaMessage is what whatsmeow needs to download and what we need to size and name a file.
type mediaMessage interface {
	proto.Message
	whatsmeow.DownloadableMessage
	GetMimetype() string
	GetFileLength() uint64
}

// encodeRef serialises the media descriptor so it can be stored by the C side
// and handed back later for on-demand download.
func encodeRef(kind string, msg proto.Message) string {
	raw, err := proto.Marshal(msg)
	if err != nil {
		return ""
	}
	return kind + ":" + base64.StdEncoding.EncodeToString(raw)
}

func decodeRef(ref string) (mediaMessage, string, error) {
	kind, payload, ok := strings.Cut(ref, ":")
	if !ok {
		return nil, "", fmt.Errorf("malformed media reference")
	}
	raw, err := base64.StdEncoding.DecodeString(payload)
	if err != nil {
		return nil, "", err
	}
	var msg mediaMessage
	switch kind {
	case "image":
		msg = &waE2E.ImageMessage{}
	case "video":
		msg = &waE2E.VideoMessage{}
	case "audio":
		msg = &waE2E.AudioMessage{}
	case "document":
		msg = &waE2E.DocumentMessage{}
	case "sticker":
		msg = &waE2E.StickerMessage{}
	default:
		return nil, "", fmt.Errorf("unknown media kind %q", kind)
	}
	if err := proto.Unmarshal(raw, msg); err != nil {
		return nil, "", err
	}
	return msg, kind, nil
}

func extensionFor(mime, kind string) string {
	mime = strings.ToLower(strings.TrimSpace(strings.Split(mime, ";")[0]))
	known := map[string]string{
		"image/jpeg": ".jpg", "image/png": ".png", "image/webp": ".webp", "image/gif": ".gif",
		"video/mp4": ".mp4", "video/3gpp": ".3gp", "video/quicktime": ".mov",
		"audio/ogg": ".ogg", "audio/mpeg": ".mp3", "audio/mp4": ".m4a", "audio/aac": ".aac",
		"application/pdf": ".pdf",
	}
	if ext, ok := known[mime]; ok {
		return ext
	}
	switch kind {
	case "image", "sticker":
		return ".jpg"
	case "video":
		return ".mp4"
	case "audio":
		return ".ogg"
	}
	return ".bin"
}

func (s *Session) download(cmd Command) {
	fail := func(detail string) {
		s.emit(map[string]any{"evt": "error", "id": cmd.ID, "detail": detail})
	}
	if !isSafeID(cmd.ID) {
		fail("Invalid message id.")
		return
	}
	if s.client == nil || !s.client.IsLoggedIn() {
		fail("Not connected; media will download once WhatsApp is back.")
		return
	}
	msg, kind, err := decodeRef(cmd.Ref)
	if err != nil {
		fail("This message has no downloadable media.")
		return
	}
	if cmd.MaxMB > 0 && msg.GetFileLength() > uint64(cmd.MaxMB)*1024*1024 {
		fail(fmt.Sprintf("Media is larger than %d MB; click it to download.", cmd.MaxMB))
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 5*time.Minute)
	defer cancel()
	data, err := s.client.Download(ctx, msg)
	if err != nil {
		fail("Download failed: " + err.Error())
		return
	}
	// The file name is derived only from the validated id, never from remote input.
	path := filepath.Join(s.cfg.MediaDir, cmd.ID+extensionFor(msg.GetMimetype(), kind))
	if err := os.WriteFile(path, data, 0o600); err != nil {
		fail("Could not save media: " + err.Error())
		return
	}
	s.emit(map[string]any{"evt": "media", "id": cmd.ID, "path": path})
}
