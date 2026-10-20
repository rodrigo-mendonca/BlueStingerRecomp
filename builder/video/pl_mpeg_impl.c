/* N5 (D1): implementacao do pl_mpeg (third_party/pl_mpeg, MIT, commit fixo no README de la). */
#include <stdio.h>
#include <stdlib.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

/* Fim do stream com o ES inteiro em memoria (o caso do Builder). Dois defeitos do pl_mpeg, conferidos
   contra o ffmpeg nos SFD do disco (faltavam os 2 ultimos quadros):
   1. plm_buffer_discard_read_bytes encolhe `length`, mas o fim so e declarado com length == total_size:
      num buffer de memoria isso nunca acontece, e a ultima imagem fica esperando um proximo codigo de
      inicio. Aqui o fim e declarado (todo o ES ja esta no buffer) e a decodificacao continua;
   2. a referencia pendente so sai se a ultima imagem for I/P; se o filme terminar em B, ela e
      devolvida aqui.
   Chamado depois de plm_video_decode devolver NULL; devolve NULL quando nao ha mais quadros. Sem
   alterar o pl_mpeg.h. */
plm_frame_t *bs_plm_video_flush(plm_video_t *self) {
	if (!self->buffer->has_ended) {
		self->buffer->has_ended = TRUE;
		plm_frame_t *f = plm_video_decode(self);
		if (f) return f;
	}
	if (!self->has_reference_frame || self->assume_no_b_frames) return NULL;
	self->has_reference_frame = FALSE;
	self->frame_backward.time = self->time;
	return &self->frame_backward;
}
