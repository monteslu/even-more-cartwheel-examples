/* The engine's Ogg Vorbis decode, outside the engine: stb_vorbis driven exactly as cartwheel's src/audio/decode.c
 * drives it (open from memory, ask for 1 or 2 output channels, pull stb_vorbis_get_samples_float_interleaved until
 * it returns 0). Built to WASI by tools/stb-vorbis/build.mjs, run by tools/lib/stbvorbis.mjs, so the alignment
 * check can decode the shipped tracks the way the game will.
 *
 *   stbdec <in.ogg> <out.f32>   writes interleaved float32, prints "channels rate frames stream_length" */
#include <stdio.h>
#include <stdlib.h>

#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: stbdec in.ogg out.f32\n"); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *data = malloc((size_t)len);
  if (fread(data, 1, (size_t)len, f) != (size_t)len) { fprintf(stderr, "short read\n"); return 1; }
  fclose(f);
  int err = 0;
  stb_vorbis *v = stb_vorbis_open_memory(data, (int)len, &err, NULL);
  if (!v) { fprintf(stderr, "stb_vorbis_open_memory failed (%d)\n", err); return 1; }
  stb_vorbis_info info = stb_vorbis_get_info(v);
  int ch = info.channels > 1 ? 2 : 1;
  unsigned n = stb_vorbis_stream_length_in_samples(v);
  FILE *o = fopen(argv[2], "wb");
  if (!o) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
  static float buf[4096 * 2];
  long frames = 0;
  for (;;) {
    int got = stb_vorbis_get_samples_float_interleaved(v, ch, buf, 4096 * ch);
    if (got <= 0) break;
    fwrite(buf, sizeof(float), (size_t)got * ch, o);
    frames += got;
  }
  fclose(o);
  printf("%d %u %ld %u\n", ch, info.sample_rate, frames, n);
  stb_vorbis_close(v);
  free(data);
  return 0;
}
