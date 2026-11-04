#ifndef BODY_PART_H
#define BODY_PART_H

#include "../constants.h"

#include <stdbool.h>

typedef struct body_part {
  float fxs[GD_EEZ];
  float fys[GD_EEZ];
  float fltns[GD_EEZ];
  float fsmus[GD_EEZ];

  int fpos;
  int ftg;

  float xx;
  float yy;
  float fx;
  float fy;
  float ebx;
  float eby;
  float fltn;
  float fsmu;
  float ltn;
  float iang;
  float da;
  float smu;

  bool iiv;
  bool dying;
  /* Temporary point added by head_ext.c to bridge the gap between the last
     real point and a head drawn ahead of it. Never sent by the server. */
  bool virt;
} body_part;

#endif
