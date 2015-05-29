#include "Common/CommonWindows.h"
#include "native/gfx_es2/gl_state.h"
#include "native/gfx/gl_common.h"
#include "GL/gl.h"
#include "GL/wglew.h"
#include "Core/Config.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"

#include "Windows/OpenGLBase.cpp"
#include "Windows/OpenGLBase.h"
#include "Windows/W32Util/Misc.h"

static HDC hDC;     // Private GDI Device Context
static HGLRC hRC;   // Permanent Rendering Context
static HWND hWnd;   // Holds Our Window Handle

static int xres, yres;

// TODO: Make config?
static bool enableGLDebug = false;

void AsyncVideoFileView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
        // TODO: involve sizemode
        if (video_) {
                float vidw = (float)video_->Width();
                float vidh = (float)video_->Height();
   	            switch (sizeMode_) {
	            	case UI::IS_FIXED:
			                  w = fixedSizeW_;
			                  h = fixedSizeH_;
			                  break;
		            case UI::IS_DEFAULT:
		            default:
		                    w = vidw;
			                  h = vidh;
			                  break;
		            }
	      } else {
		            w = 480;
		            h = 272;
	      }
}

void AsyncVideofileView::SetFilename(std::string filename) {
             if (filename_ != filename) {
                           videoFailed_ = false;
                           filename_ = filename;
                           if (video_) {
                                         video_->Release();
                                         video_ = nullptr;
                                         
                           }
             }
}

void AsyncVideofileView::SetFilefolder(std::string filefolder) {
             if (filefolder_ != filefolder) {
                             videoFailed_ = false;
                             filefolder_ = filefolder;
                             if (video_) {
                                           video_->Release();
                                           video_ = nullptr
                            }
             }
}
void GameScreen::CreateButtons() {
             static const int NUM_BUTTONS = 3
      using namespace UI;
            Margins scrollMargins(0, 20, 0, 0);
    	      Margins actionMenuMargins(0, 20, 15, 0);
    	      I18NCategory *c = GetI18NCategory("Capture");
    	      I18NCategory *s = GetI18NCategory("Stop Capture");
    	      I18NCategory *i = GetI18NCategory("Pause Capture");
    	      root_ = new LinearLayout(ORIENT_VERTICAL);
    	      
    	      ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0, scrollMargins));
            root_->Add(leftColumn);
    	      
    	      leftColumnItems->Add(new Spacer(0.0));
	          leftColumnItems->SetSpacing(10.0);
	          for (int i = 0; i < NUM_BUTTONS; i++) {
		                      ButtonView *button = leftColumnItems->Add(new ButtonView(i, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		                      button->OnButtonLoaded.Handle(this, &GameScreen::OnButton);
		                      button->OnbuttonSaved.Handle(this, &GameScreen::OnButton);
		                      button->OnButtonClicked.Handle(this, &GameScreen::OnButtonClicked);
	          }
}
