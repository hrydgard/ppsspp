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
		void reloadCheats();
		void saveCheats();
		
		void setEngine(DarkFrostEngine *nDarkFrostEngine);

		void toggleRealAddressing();
		bool getRealAddressing() const;

		void toggleCheatsEnabled();
		bool getCheatsEnabled() const;

		unsigned char getASCII(unsigned int, unsigned int);

		//VARIABLES
		std::string gameDir="ms0:/darkfrost/codes/__________.txt";
		std::string gameId;//length of 10
		bool cheatsEnabled;
		bool realAddressing;
		int valueFormat;
};