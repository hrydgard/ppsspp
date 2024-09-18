#pragma once

#include <cstring>
#include <wrl/client.h>

#include "Common/GPU/D3D9/D3D9ShaderCompiler.h"

// TODO: Get rid of these somehow.
extern Microsoft::WRL::ComPtr<IDirect3DDevice9> pD3Ddevice9;
extern Microsoft::WRL::ComPtr<IDirect3DDevice9Ex> pD3DdeviceEx9;

class DirectXState {
private:
	template<D3DRENDERSTATETYPE cap, bool init>
	class BoolState {
		bool _value;
	public:
		BoolState() : _value(init) {
			DirectXState::state_count++;
    }

		inline void set(bool value) {
			if (_value != value) {
				_value = value;
				restore();
			}
		}
		void force(bool value) {
			bool old = _value;
			set(value);
			_value = old;
		}
		inline void enable() {
			set(true);
		}
		inline void disable() {
			set(false);
		}
		operator bool() const {
			return isset();
		}
		inline bool isset() {
			return _value;
		}
		void restore() {
			pD3Ddevice9->SetRenderState(cap, _value);
		}
	};

	template<D3DRENDERSTATETYPE state1, DWORD p1def>
	class DxState1 {
		D3DRENDERSTATETYPE _state1;
		DWORD p1;
	public:
		DxState1() : _state1(state1), p1(p1def) {
			DirectXState::state_count++;
    }

		inline void set(DWORD newp1) {
			if (p1 != newp1) {
				p1 = newp1;
				restore();
			}
		}
		void force(DWORD newp1) {
			DWORD old = p1;
			set(newp1);
			p1 = old;
		}
		void restore() {
			pD3Ddevice9->SetRenderState(_state1, p1);
		}
	};

	template<D3DSAMPLERSTATETYPE state1, DWORD p1def>
	class DxSampler0State1 {
		D3DSAMPLERSTATETYPE _state1;
		DWORD p1;
	public:
		DxSampler0State1() : _state1(state1), p1(p1def) {
			DirectXState::state_count++;
		}

		inline void set(DWORD newp1) {
			if (p1 != newp1) {
				p1 = newp1;
				restore();
			}
		}
		void force(DWORD newp1) {
			DWORD old = p1;
			set(newp1);
			p1 = old;
		}
		void restore() {
			pD3Ddevice9->SetSamplerState(0, _state1, p1);
		}
	};

	// Can't have FLOAT template parameters...
	template<D3DSAMPLERSTATETYPE state1, DWORD p1def>
	class DxSampler0State1Float {
		D3DSAMPLERSTATETYPE _state1;
		union {
			FLOAT p1;
			DWORD p1d;
		};
	public:
		DxSampler0State1Float() : _state1(state1), p1d(p1def) {
			DirectXState::state_count++;
		}

		inline void set(FLOAT newp1) {
			if (p1 != newp1) {
				p1 = newp1;
				restore();
			}
		}
		void force(FLOAT newp1) {
			FLOAT old = p1;
			set(newp1);
			p1 = old;
		}
		void restore() {
			pD3Ddevice9->SetSamplerState(0, _state1, p1d);
		}
	};

	template<D3DRENDERSTATETYPE state1, DWORD p1def, D3DRENDERSTATETYPE state2, DWORD p2def>
	class DxState2 {
		D3DRENDERSTATETYPE _state1;
		D3DRENDERSTATETYPE _state2;
		DWORD p1;
		DWORD p2;
	public:
		DxState2() : _state1(state1),_state2(state2), p1(p1def), p2(p2def) {
			DirectXState::state_count++;
    }

		inline void set(DWORD newp1, DWORD newp2) {
			if (p1 != newp1) {
				p1 = newp1;
				pD3Ddevice9->SetRenderState(_state1, p1);
			}
			if (p2 != newp2) {
				p2 = newp2;
				pD3Ddevice9->SetRenderState(_state2, p2);
			}
		}
		void force(DWORD newp1, DWORD newp2) {
			DWORD old1 = p1;
			DWORD old2 = p2;
			set(newp1, newp2);
			p1 = old1;
			p2 = old2;
		}
		void restore() {
			pD3Ddevice9->SetRenderState(_state1, p1);
			pD3Ddevice9->SetRenderState(_state2, p2);
		}
	};

	template<D3DRENDERSTATETYPE state1, DWORD p1def, D3DRENDERSTATETYPE state2, DWORD p2def, D3DRENDERSTATETYPE state3, DWORD p3def>
	class DxState3 {
		D3DRENDERSTATETYPE _state1;
		D3DRENDERSTATETYPE _state2;
		D3DRENDERSTATETYPE _state3;
		DWORD p1;
		DWORD p2;
		DWORD p3;
	public:
		DxState3() : _state1(state1),_state2(state2), _state3(state3), 
			p1(p1def), p2(p2def), p3(p3def) {
    }

		inline void set(DWORD newp1, DWORD newp2, DWORD newp3) {
			if (p1 != newp1) {
				p1 = newp1;
				pD3Ddevice9->SetRenderState(_state1, p1);
			}
			if (p2 != newp2) {
				p2 = newp2;
				pD3Ddevice9->SetRenderState(_state2, p2);
			}
			if (p3 != newp3) {
				p3 = newp3;
				pD3Ddevice9->SetRenderState(_state3, p3);
			}
		}
		void force(DWORD newp1, DWORD newp2, DWORD newp3) {
			DWORD old1 = p1;
			DWORD old2 = p2;
			DWORD old3 = p3;
			set(newp1, newp2, newp3);
			p1 = old1;
			p2 = old2;
			p3 = old3;
		}
		void restore() {
			pD3Ddevice9->SetRenderState(_state1, p1);
			pD3Ddevice9->SetRenderState(_state2, p2);
			pD3Ddevice9->SetRenderState(_state3, p3);
		}
	};

	template<D3DRENDERSTATETYPE state1, DWORD p1def, D3DRENDERSTATETYPE state2, DWORD p2def, D3DRENDERSTATETYPE state3, DWORD p3def, D3DRENDERSTATETYPE state4, DWORD p4def>
	class DxState4 {
		D3DRENDERSTATETYPE _state1;
		D3DRENDERSTATETYPE _state2;
		D3DRENDERSTATETYPE _state3;
		D3DRENDERSTATETYPE _state4;
		DWORD p1;
		DWORD p2;
		DWORD p3;
		DWORD p4;
	public:
		DxState4() : _state1(state1), _state2(state2), _state3(state3), _state4(state4),
			p1(p1def), p2(p2def), p3(p3def), p4(p4def) {
		}

		inline void set(DWORD newp1, DWORD newp2, DWORD newp3, DWORD newp4) {
			if (p1 != newp1) {
				p1 = newp1;
				pD3Ddevice9->SetRenderState(_state1, p1);
			}
			if (p2 != newp2) {
				p2 = newp2;
				pD3Ddevice9->SetRenderState(_state2, p2);
			}
			if (p3 != newp3) {
				p3 = newp3;
				pD3Ddevice9->SetRenderState(_state3, p3);
			}
			if (p4 != newp4) {
				p4 = newp4;
				pD3Ddevice9->SetRenderState(_state4, p4);
			}
		}
		void force(DWORD newp1, DWORD newp2, DWORD newp3, DWORD newp4) {
			DWORD old1 = p1;
			DWORD old2 = p2;
			DWORD old3 = p3;
			DWORD old4 = p4;
			set(newp1, newp2, newp3, newp4);
			p1 = old1;
			p2 = old2;
			p3 = old3;
			p4 = old4;
		}
		void restore() {
			pD3Ddevice9->SetRenderState(_state1, p1);
			pD3Ddevice9->SetRenderState(_state2, p2);
			pD3Ddevice9->SetRenderState(_state3, p3);
			pD3Ddevice9->SetRenderState(_state4, p4);
		}
	};

	
	class SavedBlendFactor {
		DWORD c;
	public:
		SavedBlendFactor() {
			c = 0xFFFFFFFF;
			DirectXState::state_count++;
		}
		inline void set(const float v[4]) {
			DWORD newc = D3DCOLOR_COLORVALUE(v[0], v[1], v[2], v[3]);
			if (c != newc) {
				c = newc;
				restore();
			}
		}
		void setDWORD(DWORD newc) {
			newc = ((newc >> 8) & 0xff) | (newc & 0xff00ff00) | ((newc << 16) & 0xff0000);  // ARGB -> ABGR fix
			if (c != newc) {
				c = newc;
				restore();
			}
		}
		void force(const float v[4]) {
			DWORD old = c;
			set(v);
			c = old;
		}
		inline void restore() {
			pD3Ddevice9->SetRenderState(D3DRS_BLENDFACTOR, c);
		}
	};

	class StateVp {
		D3DVIEWPORT9 viewport;
	public:
		StateVp() {
			memset(&viewport, 0, sizeof(viewport));
			// It's an error if w/h is zero, so let's start with something that can work.
			viewport.Width = 1;
			viewport.Height = 1;
		}
		inline void set(int x, int y, int w, int h, float n = 0.f, float f = 1.f) {
			D3DVIEWPORT9 newviewport;
			newviewport.X = x;
			newviewport.Y = y;
			newviewport.Width = w;
			newviewport.Height = h;
			newviewport.MinZ = n;
			newviewport.MaxZ = f;
			if (memcmp(&viewport, &newviewport, sizeof(viewport)) != 0) {
				viewport = newviewport;
				restore();
			}
		}

		void force(int x, int y, int w, int h, float n = 0.f, float f = 1.f) {
			D3DVIEWPORT9 old = viewport;
			set(x, y, w, h, n, f);
			viewport = old;
		}

		inline void restore() {
			pD3Ddevice9->SetViewport(&viewport);
		}
	};

	class StateScissor {
		RECT rect;
	public:
		inline void set(int x1, int y1, int x2, int y2) {
			RECT newrect = {x1, y1, x2, y2};
			if (memcmp(&rect, &newrect, sizeof(rect))) {
				rect = newrect;
				restore();
			}
		}

		void force(int x1, int y1, int x2, int y2) {
			RECT old = rect;
			set(x1, y1, x2, y2);
			rect = old;
		}

		inline void restore() {
			pD3Ddevice9->SetScissorRect(&rect);
		}
	};

	bool initialized;

public:
	static int state_count;
	DirectXState() : initialized(false) {}
	void Initialize();
	void Restore();

	// When adding a state here, don't forget to add it to DirectxState::Restore() too
	BoolState<D3DRS_ALPHABLENDENABLE, false> blend;
	BoolState<D3DRS_SEPARATEALPHABLENDENABLE, false> blendSeparate;
	DxState4<D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, D3DRS_SRCBLENDALPHA, D3DBLEND_ONE, D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO> blendFunc;
	DxState2<D3DRS_BLENDOP, D3DBLENDOP_ADD, D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD> blendEquation;
	SavedBlendFactor blendColor;

	BoolState<D3DRS_SCISSORTESTENABLE, false> scissorTest;

	DxState1<D3DRS_CULLMODE, D3DCULL_NONE> cullMode;
	DxState1<D3DRS_SHADEMODE, D3DSHADE_GOURAUD> shadeMode;
	DxState1<D3DRS_CLIPPLANEENABLE, 0> clipPlaneEnable;

	BoolState<D3DRS_ZENABLE, false> depthTest;

	DxState1<D3DRS_ALPHAFUNC, D3DCMP_ALWAYS> alphaTestFunc;
	DxState1<D3DRS_ALPHAREF, 0> alphaTestRef;
	BoolState<D3DRS_ALPHATESTENABLE, false> alphaTest;

	DxState1<D3DRS_ZFUNC, D3DCMP_LESSEQUAL> depthFunc;
	DxState1<D3DRS_ZWRITEENABLE, TRUE> depthWrite;

	DxState1<D3DRS_COLORWRITEENABLE, 0xF> colorMask;

	StateVp viewport;
	StateScissor scissorRect;

	BoolState<D3DRS_STENCILENABLE, false> stencilTest;

	DxState3<D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP, D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP, D3DRS_STENCILPASS, D3DSTENCILOP_KEEP> stencilOp;
	DxState1<D3DRS_STENCILFUNC, D3DCMP_ALWAYS> stencilFunc;
	DxState1<D3DRS_STENCILREF, 0> stencilRef;
	DxState1<D3DRS_STENCILWRITEMASK, 0xFFFFFFFF> stencilWriteMask;
	DxState1<D3DRS_STENCILMASK, 0xFFFFFFFF> stencilCompareMask;

	DxSampler0State1<D3DSAMP_MINFILTER, D3DTEXF_POINT> texMinFilter;
	DxSampler0State1<D3DSAMP_MAGFILTER, D3DTEXF_POINT> texMagFilter;
	DxSampler0State1<D3DSAMP_MIPFILTER, D3DTEXF_NONE> texMipFilter;
	DxSampler0State1Float<D3DSAMP_MIPMAPLODBIAS, 0> texMipLodBias;
	DxSampler0State1<D3DSAMP_MAXMIPLEVEL, 0> texMaxMipLevel;
	DxSampler0State1<D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP> texAddressU;
	DxSampler0State1<D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP> texAddressV;
	DxSampler0State1<D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP> texAddressW;
};

#undef STATE1
#undef STATE2

extern DirectXState dxstate;
