#ifndef VIEWUTILS_H
#define VIEWUTILS_H

class GUIWindow;
struct GUIPoint;
#include "Application/Views/ViewData.h"

void DrawLabeledField(GUIWindow &w, GUIPoint position, char *buffer);

bool goProjectSamplesDir(ViewData *viewData_);

#endif
