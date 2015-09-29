#include <string>
#include <vector>
#include <iostream>
#include <sstream>

#include "base/basictypes.h"
#include "Core/MemMap.h"

//class PointerWrap;

#define _DF

class DarkFrostEngine
{
	public:
		DarkFrostEngine();
		void loadCheats();
		void saveCheats();
		
		void setEngine(DarkFrostEngine *nDarkFrostEngine);

		void toggleRealAddressing();
		bool getRealAddressing();

		void toggleCheatsEnabled();
		bool getCheatsEnabled();
};