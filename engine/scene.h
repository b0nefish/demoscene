#ifndef __ENGINE_SCENE_H__
#define __ENGINE_SCENE_H__

#include "engine/object.h"

typedef struct Scene SceneT;

SceneT *NewScene();
void SceneAddObject(SceneT *self, SceneObjectT *object);
MatrixStack3D *GetObjectTranslation(SceneT *self, const StrT name);
void RenderScene(SceneT *self, CanvasT *canvas);

#endif