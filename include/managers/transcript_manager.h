#ifndef APP_MANAGERS_TRANSCRIPT_MANAGER_H
#define APP_MANAGERS_TRANSCRIPT_MANAGER_H

#include "core/chat.h"
#include "core/chat_transcript_choice.h"
#include "core/message.h"
#include "core/transcript.h"
#include "core/transcript_save_result.h"
#include "managers/transcript_manager_deps.h"

/* The transcripts of one account's voice notes: keeping the ones a
 * transcriber hands over, finding the one to show, and your choices about
 * them for each chat. It transcribes nothing itself. */
typedef struct TranscriptManager TranscriptManager;

TranscriptManager *transcript_manager_create(const TranscriptManagerDeps *deps);
void               transcript_manager_destroy(TranscriptManager *mgr);
/* Why the last save or choice was refused. */
const char        *transcript_manager_error(TranscriptManager *mgr);

/* Keeps the words of `message`, a voice note in `chat`, replacing the
 * transcript it already has in that language. `source` names the program
 * that hands them over. */
TranscriptSaveResult transcript_manager_save(TranscriptManager *mgr, const Message *message, const Chat *chat,
                                             const char *language, const char *text, const char *model, const char *source);
/* Every transcript a message has, newest first; the caller frees `*out` with transcript_array_free. */
int  transcript_manager_find(TranscriptManager *mgr, const char *message_id, Transcript **out, int *count);
/* The one to show, in the first of your transcription languages it has.
 * Fills `out` (the caller disposes it) and returns 0, or -1 when there is
 * none. *others, when given, receives how many more the message has. */
int  transcript_manager_best(TranscriptManager *mgr, const char *message_id, Transcript *out, int *others);

/* Whether a chat's transcripts show in the conversation: the setting, and what you chose for the chat. */
int  transcript_manager_shown(TranscriptManager *mgr, const char *chat_jid);
ChatTranscriptChoice transcript_manager_display_choice(TranscriptManager *mgr, const char *chat_jid);
int  transcript_manager_set_display_choice(TranscriptManager *mgr, const char *chat_jid, ChatTranscriptChoice choice);
/* Whether new voice notes in a chat may be transcribed. Switching it off
 * removes nothing: the transcripts the chat already has are kept. */
int  transcript_manager_transcribing(TranscriptManager *mgr, const Chat *chat);
/* What you chose for the chat, whatever else stands in the way (a lock). */
int  transcript_manager_transcribe_chosen(TranscriptManager *mgr, const char *chat_jid);
int  transcript_manager_set_transcribing(TranscriptManager *mgr, const char *chat_jid, int on);

/* An older voice note that is looked at and has no transcript goes on the
 * list of those waiting for one, once: only while voice notes are transcribed
 * as they arrive (the setting), only someone else's, and only in a chat that
 * is transcribed. */
void transcript_manager_want(TranscriptManager *mgr, const Message *message, const Chat *chat);
/* The voice note that has waited longest (returns 0 when none waits), and taking it off the list. */
int  transcript_manager_next_wanted(TranscriptManager *mgr, char *message_id, unsigned long size);
void transcript_manager_drop_wanted(TranscriptManager *mgr);

/* True once after a transcript was kept or a choice changed, so the screen can be drawn again. */
int  transcript_manager_take_changed(TranscriptManager *mgr);

#endif
