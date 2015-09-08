/*
 * Copyright (c) 2013 Antti Pohjola
 *
 */
//Adds mediakey support for Symbian (volume up/down)

#include <QApplication>
#include "SymbianMediaKeys.h"
#include "input/keycodes.h"
#include "input/input_state.h"
#include "base/NativeApp.h"

#define KTimeOut 80

SymbianMediaKeys::SymbianMediaKeys()
	: CActive ( EPriorityNormal ){
	CActiveScheduler::Add( this );
	iInterfaceSelector = CRemConInterfaceSelector::NewL();
	iRemConCore = CRemConCoreApiTarget::NewL(*iInterfaceSelector, *this);
	iInterfaceSelector->OpenTargetL();
	
	playtimer = new QTimer(this);
	connect(playtimer, SIGNAL(timeout()), this, SLOT(playtimerexpired()));
	stoptimer = new QTimer(this);
	connect(stoptimer, SIGNAL(timeout()), this, SLOT(stoptimerexpired()));
	forwardtimer = new QTimer(this);
	connect(forwardtimer, SIGNAL(timeout()), this, SLOT(forwardtimerexpired()));
	backwardtimer = new QTimer(this);
	connect(backwardtimer, SIGNAL(timeout()), this, SLOT(backwardtimerexpired()));
	voluptimer = new QTimer(this);
	connect(voluptimer, SIGNAL(timeout()), this, SLOT(voluptimerexpired()));
	voldowntimer = new QTimer(this);
	connect(voldowntimer, SIGNAL(timeout()), this, SLOT(voldowntimerexpired()));
}

SymbianMediaKeys::~SymbianMediaKeys(){
	delete iInterfaceSelector;
	iRemConCore = NULL; //owned by interfaceselector
	Cancel();
	iResponseQ.Reset();
	iResponseQ.Close();
}

void SymbianMediaKeys::subscribeKeyEvent(QObject* aObject ){
	receiver = aObject;
}

/*
 * it seems that it takes about 600ms to get an update after buttonpress
 * */
void SymbianMediaKeys::MrccatoCommand(TRemConCoreApiOperationId aOperationId,TRemConCoreApiButtonAction aButtonAct){
	TRequestStatus status;
	switch( aOperationId ){
	case ERemConCoreApiPausePlayFunction:
	{
		switch (aButtonAct){
		case ERemConCoreApiButtonPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PLAY_PAUSE, KEY_DOWN));
			break;
		case ERemConCoreApiButtonRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PLAY_PAUSE, KEY_UP));
			break;
		case ERemConCoreApiButtonClick:
			playtimer->start(KTimeOut);
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PLAY_PAUSE, KEY_DOWN));
			break;
		default:
			// Play/Pause unknown action
			break;
		}
		break;
	}

	case ERemConCoreApiStop:
	{
		switch (aButtonAct){
		case ERemConCoreApiButtonPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_STOP, KEY_DOWN));
			break;
		case ERemConCoreApiButtonRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_STOP, KEY_UP));
			break;
		case ERemConCoreApiButtonClick:
			stoptimer->start(KTimeOut);
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_STOP, KEY_DOWN));
			break;
		default:
			break;
		}
		break;
	}
	case ERemConCoreApiRewind:
	{
		switch (aButtonAct){
		case ERemConCoreApiButtonPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PREVIOUS, KEY_DOWN));
			break;
		case ERemConCoreApiButtonRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PREVIOUS, KEY_UP));
			break;
		case ERemConCoreApiButtonClick:
			backwardtimer->start(KTimeOut);
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PREVIOUS, KEY_DOWN));
		default:
			break;
		}
		break;
	}
	case ERemConCoreApiFastForward:
	{
		switch (aButtonAct){
		case ERemConCoreApiButtonPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_NEXT, KEY_DOWN));
			break;
		case ERemConCoreApiButtonRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_NEXT, KEY_UP));
			break;
		case ERemConCoreApiButtonClick:
			forwardtimer->start(KTimeOut);
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_NEXT, KEY_DOWN));
		default:
			break;
		}
		break;
	}
	case ERemConCoreApiVolumeUp:
	{
		switch (aButtonAct){
		case ERemConCoreApiButtonPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_VOLUME_UP, KEY_DOWN));
			break;
		case ERemConCoreApiButtonRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_VOLUME_UP, KEY_UP));
			break;
		case ERemConCoreApiButtonClick:
			voluptimer->start(KTimeOut);
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_VOLUME_UP, KEY_DOWN));
		default:
			break;
		}
		break;
	}
	case ERemConCoreApiVolumeDown:
	{
		switch (aButtonAct){
		case ERemConCoreApiButtonPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_VOLUME_DOWN, KEY_DOWN));
			break;
		case ERemConCoreApiButtonRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_VOLUME_DOWN, KEY_UP));
			break;
		case ERemConCoreApiButtonClick:
			voldowntimer->start(KTimeOut);
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_VOLUME_DOWN, KEY_DOWN));
		default:
			break;
		}
		break;
	}
	case ERemConCoreApiBackward:
	{
		switch (aButtonAct)
		{
		case ERemConCoreApiButtonPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PREVIOUS, KEY_DOWN));
			break;
		case ERemConCoreApiButtonRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PREVIOUS, KEY_UP));
			break;
		case ERemConCoreApiButtonClick:
			backwardtimer->start(KTimeOut);
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PREVIOUS, KEY_DOWN));
		default:
			break;
		}
		break;
	}
	case ERemConCoreApiForward:
	{
		switch (aButtonAct)
		{
		case ERemConCoreApiButtonPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_NEXT, KEY_DOWN));
			break;
		case ERemConCoreApiButtonRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_NEXT, KEY_UP));
			break;
		case ERemConCoreApiButtonClick:
			forwardtimer->start(KTimeOut);
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_NEXT, KEY_DOWN));
		default:
			break;
		}
		break;
	}

	default:
		break;
	}
	//complete key event
	CompleteMediaKeyEvent( aOperationId );
}

void SymbianMediaKeys::MrccatoPlay(TRemConCoreApiPlaybackSpeed aSpeed,TRemConCoreApiButtonAction aButtonAct){

}

void SymbianMediaKeys::MrccatoTuneFunction(TBool aTwoPart, TUint aMajorChannel,TUint aMinorChannel,TRemConCoreApiButtonAction aButtonAct){

}

void SymbianMediaKeys::MrccatoSelectDiskFunction(TUint aDisk, TRemConCoreApiButtonAction aButtonAct){

}

void SymbianMediaKeys::MrccatoSelectAvInputFunction(TUint8 aAvInputSignalNumber,TRemConCoreApiButtonAction aButtonAct){

}

void SymbianMediaKeys::MrccatoSelectAudioInputFunction(TUint8 aAudioInputSignalNumber,TRemConCoreApiButtonAction aButtonAct){

}

void SymbianMediaKeys::CompleteMediaKeyEvent( TRemConCoreApiOperationId aOperationId ){
	if( !IsActive() ){
		switch ( aOperationId )
		{
			case ERemConCoreApiVolumeUp:
			{
				iRemConCore->VolumeUpResponse( iStatus, KErrNone );
				SetActive();
				break;
			}

			case ERemConCoreApiVolumeDown:
			{
				iRemConCore->VolumeDownResponse( iStatus, KErrNone );
				SetActive();
				break;
			}
			case ERemConCoreApiPlay:
			{
				iRemConCore-> PlayResponse(iStatus, KErrNone);
				SetActive();
				break;
			}
			case ERemConCoreApiStop:
			{
				iRemConCore->StopResponse(iStatus, KErrNone);
				SetActive();
				break;
			}
			case ERemConCoreApiPause:
			{
				iRemConCore->PauseResponse(iStatus, KErrNone);
				SetActive();
				break;
			}
			case ERemConCoreApiRewind:
			{
				iRemConCore->RewindResponse(iStatus, KErrNone);
				SetActive();
				break;
			}
			case ERemConCoreApiFastForward:
			{
				iRemConCore->FastForwardResponse(iStatus, KErrNone);
				SetActive();
				break;
			}
			case ERemConCoreApiForward:
			{
				iRemConCore->ForwardResponse( iStatus, KErrNone );
				SetActive();
				break;
			}
			case ERemConCoreApiBackward:
			{
				iRemConCore->BackwardResponse(iStatus, KErrNone );
				SetActive();
				break;
			}
			default:
				break;
		}
	}
	else{
		//active, append to queue
		iResponseQ.Append( aOperationId );
	}
}

void SymbianMediaKeys::RunL(){
	if ( iResponseQ.Count() ){
		CompleteMediaKeyEvent( iResponseQ[0] );
		//remove old response from que
		iResponseQ.Remove(0);
		iResponseQ.Compress();
	}
}

void SymbianMediaKeys::DoCancel(){
}

void SymbianMediaKeys::playtimerexpired(){
	playtimer->stop();
	NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PLAY_PAUSE, KEY_UP));
}

void SymbianMediaKeys::stoptimerexpired(){
	stoptimer->stop();
	NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_STOP, KEY_UP));
}

void SymbianMediaKeys::forwardtimerexpired(){
	forwardtimer->stop();
	NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_NEXT, KEY_UP));
}

void SymbianMediaKeys::backwardtimerexpired(){
	backwardtimer->stop();
	NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_MEDIA_PREVIOUS, KEY_UP));
}

void SymbianMediaKeys::voluptimerexpired(){
	voluptimer->stop();
	NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_VOLUME_UP, KEY_UP));
}

void SymbianMediaKeys::voldowntimerexpired(){
	voldowntimer->stop();
	NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_VOLUME_DOWN, KEY_UP));
}
