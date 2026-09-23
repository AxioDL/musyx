#include "hw_pc_assets.h"
#include "musyx/sal.h"

typedef struct PCSong {
  struct PCSong *next;
  void *data;
  u32 references;
} PCSong;
static PCSong *songs;

void *salPCAcquireSong(const void *input) {
  if (!input)
    return NULL;
  for (PCSong *song = songs; song; song = song->next) {
    if (song->data == input) {
      ++song->references;
      return song->data;
    }
  }
  void *data;
  if (!salPCDecodeArrangement((SND_PC_SPAN){input, SIZE_MAX}, &data, NULL, NULL))
    return NULL;
  PCSong *song = salMalloc(sizeof(*song));
  if (!song) {
    salFree(data);
    return NULL;
  }
  *song = (PCSong){songs, data, 1};
  songs = song;
  return data;
}

void salPCReleaseSong(void *data) {
  for (PCSong *song = songs; song; song = song->next) {
    if (song->data == data && song->references) {
      --song->references;
      return;
    }
  }
}

void salPCCollectSongs(void) {
  PCSong **link = &songs;
  while (*link) {
    PCSong *song = *link;
    if (song->references) {
      link = &song->next;
      continue;
    }
    *link = song->next;
    salFree(song->data);
    salFree(song);
  }
}
