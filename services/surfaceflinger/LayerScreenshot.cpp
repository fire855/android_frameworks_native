/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

#include <GLES/gl.h>
#include <GLES/glext.h>

#include <utils/Errors.h>
#include <utils/Log.h>

#include <ui/GraphicBuffer.h>

#include "LayerScreenshot.h"
#include "SurfaceFlinger.h"
#include "DisplayDevice.h"
#include <cutils/properties.h>

namespace android {
// ---------------------------------------------------------------------------

LayerScreenshot::LayerScreenshot(SurfaceFlinger* flinger,
        const sp<Client>& client)
    : LayerBaseClient(flinger, client),
      mTextureName(0), mFlinger(flinger), mIsSecure(false)
{
}

LayerScreenshot::~LayerScreenshot()
{
    if (mTextureName) {
        mFlinger->deleteTextureAsync(mTextureName);
    }
}

status_t LayerScreenshot::captureLocked(int32_t layerStack) {
    GLfloat u, v;
    status_t result = mFlinger->renderScreenToTextureLocked(layerStack,
            &mTextureName, &u, &v);
    if (result != NO_ERROR) {
        return result;
    }
    initTexture(u, v);

    // Currently screenshot always comes from the default display
    mIsSecure = mFlinger->getDefaultDisplayDevice()->getSecureLayerVisible();

    return NO_ERROR;
}

status_t LayerScreenshot::capture() {
    GLfloat u, v;
    status_t result = mFlinger->renderScreenToTexture(0, &mTextureName, &u, &v);
    if (result != NO_ERROR) {
        return result;
    }
    initTexture(u, v);

    // Currently screenshot always comes from the default display
    mIsSecure = mFlinger->getDefaultDisplayDevice()->getSecureLayerVisible();
    
    return NO_ERROR;
}

void LayerScreenshot::initTexture(GLfloat u, GLfloat v) {
    glBindTexture(GL_TEXTURE_2D, mTextureName);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    mTexCoords[0] = 0;         mTexCoords[1] = v;
    mTexCoords[2] = 0;         mTexCoords[3] = 0;
    mTexCoords[4] = u;         mTexCoords[5] = 0;
    mTexCoords[6] = u;         mTexCoords[7] = v;

	const sp<const DisplayDevice> hw(mFlinger->getDefaultDisplayDevice());
    if(hw->setDispProp(DISPLAY_CMD_GETDISPLAYMODE,0,0,0) == DISPLAY_MODE_SINGLE_VAR_GPU)
    {
        mDispWidth = hw->setDispProp(DISPLAY_CMD_GETDISPPARA,0,DISPLAY_VALID_WIDTH,0);
        mDispHeight = hw->setDispProp(DISPLAY_CMD_GETDISPPARA,0,DISPLAY_VALID_HEIGHT,0);

        if(hw->getOrientation() == 0)
        {
            GLfloat x = GLfloat(mDispWidth) / 1920;
            GLfloat y = GLfloat(1080 - mDispHeight) / 1080;
            
            mTexCoords[0] = 0;      mTexCoords[1] = 1;
            mTexCoords[2] = 0;      mTexCoords[3] = y;
            mTexCoords[4] = x;      mTexCoords[5] = y;
            mTexCoords[6] = x;      mTexCoords[7] = 1;
        }
        else if(hw->getOrientation() == 1)
        {
            GLfloat x = GLfloat(1920 - mDispWidth) / 1920;
            GLfloat y = GLfloat(1080 - mDispHeight) / 1080;
            
            mTexCoords[0] = x;      mTexCoords[1] = 1;
            mTexCoords[2] = x;      mTexCoords[3] = y;
            mTexCoords[4] = 1;      mTexCoords[5] = y;
            mTexCoords[6] = 1;      mTexCoords[7] = 1;
        }
        else if(hw->getOrientation() == 2)
        {
            GLfloat x = GLfloat(1920 - mDispWidth) / 1920;
            GLfloat y = GLfloat(mDispHeight) / 1080;
            
            mTexCoords[0] = x;      mTexCoords[1] = y;
            mTexCoords[2] = x;      mTexCoords[3] = 0;
            mTexCoords[4] = 1;      mTexCoords[5] = 0;
            mTexCoords[6] = 1;      mTexCoords[7] = y;
        }
        else if(hw->getOrientation() == 3)
        {
            GLfloat x = GLfloat(mDispWidth) / 1920;
            GLfloat y = GLfloat(mDispHeight) / 1080;
            
            mTexCoords[0] = 0;      mTexCoords[1] = y;
            mTexCoords[2] = 0;      mTexCoords[3] = 0;
            mTexCoords[4] = x;      mTexCoords[5] = 0;
            mTexCoords[6] = x;      mTexCoords[7] = y;
        }
    }
}

void LayerScreenshot::initStates(uint32_t w, uint32_t h, uint32_t flags) {
    LayerBaseClient::initStates(w, h, flags);
    if (!(flags & ISurfaceComposerClient::eHidden)) {
        capture();
    }
    if (flags & ISurfaceComposerClient::eSecure) {
        ALOGW("ignoring surface flag eSecure - LayerScreenshot is considered "
                "secure iff it captures the contents of a secure surface.");
    }
}

uint32_t LayerScreenshot::doTransaction(uint32_t flags)
{
    const LayerBase::State& draw(drawingState());
    const LayerBase::State& curr(currentState());

    if (draw.flags & layer_state_t::eLayerHidden) {
        if (!(curr.flags & layer_state_t::eLayerHidden)) {
            // we're going from hidden to visible
            status_t err = captureLocked(curr.layerStack);
            if (err != NO_ERROR) {
                ALOGW("createScreenshotSurface failed (%s)", strerror(-err));
            }
        }
    } else if (curr.flags & layer_state_t::eLayerHidden) {
        // we're going from visible to hidden
        if (mTextureName) {
            glDeleteTextures(1, &mTextureName);
            mTextureName = 0;
        }
    }
    return LayerBaseClient::doTransaction(flags);
}

void LayerScreenshot::onDraw(const sp<const DisplayDevice>& hw, const Region& clip) const
{
    const State& s(drawingState());
    if (s.alpha>0) {
        const GLfloat alpha = s.alpha/255.0f;
        const uint32_t fbHeight = hw->getHeight();

        if (s.alpha == 0xFF) {
            glDisable(GL_BLEND);
            glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }

        GLuint texName = mTextureName;
        if (isSecure() && !hw->isSecure()) {
            texName = mFlinger->getProtectedTexName();
        }

        LayerMesh mesh;
        computeGeometry(hw, &mesh);

        glColor4f(alpha, alpha, alpha, alpha);

        glDisable(GL_TEXTURE_EXTERNAL_OES);
        glEnable(GL_TEXTURE_2D);

        glBindTexture(GL_TEXTURE_2D, texName);
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);

        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, mTexCoords);
        //glVertexPointer(2, GL_FLOAT, 0, mesh.getVertices());
        
		char property[PROPERTY_VALUE_MAX];
		if (property_get("ro.sf.hwrotation", property, NULL) > 0) 
		{
			GLfloat mVerticesMy[4][2];
			const GLfloat *mVerticesSrc;

			mVerticesSrc = mesh.getVertices();
			switch (atoi(property)) 
			{
				case 270:	
					mVerticesMy[0][0] =  *(mVerticesSrc + 6);
					mVerticesMy[0][1] =  *(mVerticesSrc + 7);
					mVerticesMy[1][0] =  *(mVerticesSrc + 0);
					mVerticesMy[1][1] =  *(mVerticesSrc + 1);
					mVerticesMy[2][0] =  *(mVerticesSrc + 2);
					mVerticesMy[2][1] =  *(mVerticesSrc + 3);
					mVerticesMy[3][0] =  *(mVerticesSrc + 4);
					mVerticesMy[3][1] =  *(mVerticesSrc + 5);

					glVertexPointer(2, GL_FLOAT, 0, mVerticesMy);
					break;
				default:
					glVertexPointer(2, GL_FLOAT, 0, mesh.getVertices());
					break;
					
			}
		}
		else
		{
         	glVertexPointer(2, GL_FLOAT, 0, mesh.getVertices());
		}
		
        glDrawArrays(GL_TRIANGLE_FAN, 0, mesh.getVertexCount());

        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
}

// ---------------------------------------------------------------------------

}; // namespace android

