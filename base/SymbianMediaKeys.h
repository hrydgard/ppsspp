/*
 * Copyright (c) 2013 Antti Pohjola
 *
 */
//Adds mediakey support for Symbian (volume up/down)

#ifndef SYMBIANMEDIAKEYS_H_
#define SYMBIANMEDIAKEYS_H_

#include <QObject>
#include <QTimer>
#include <e32base.h>

#include <remconcoreapitargetobserver.h>    // link against RemConCoreApi.lib
#include <remconcoreapitarget.h>            // and
#include <remconinterfaceselector.h>        // RemConInterfaceBase.lib

class SymbianMediaKeys: public QObject, public CActive, public MRemConCoreApiTargetObserver
{
	Q_OBJECT
public:
	SymbianMediaKeys();
	virtual ~SymbianMediaKeys();

public:
	void subscribeKeyEvent(QObject* aObject );

public: //From MRemConCoreApiTargetObserver
	void MrccatoCommand(TRemConCoreApiOperationId aOperationId,TRemConCoreApiButtonAction aButtonAct);

	void MrccatoPlay(TRemConCoreApiPlaybackSpeed aSpeed,TRemConCoreApiButtonAction aButtonAct);

	void MrccatoTuneFunction(TBool aTwoPart,TUint aMajorChannel,TUint aMinorChannel,TRemConCoreApiButtonAction aButtonAct);

	void MrccatoSelectDiskFunction(TUint aDisk,TRemConCoreApiButtonAction aButtonAct);

	void MrccatoSelectAvInputFunction(TUint8 aAvInputSignalNumber,TRemConCoreApiButtonAction aButtonAct);

	void MrccatoSelectAudioInputFunction(TUint8 aAudioInputSignalNumber,TRemConCoreApiButtonAction aButtonAct);
	
private:
	void CompleteMediaKeyEvent( TRemConCoreApiOperationId aOperationId );
	void RunL();
	void DoCancel();
	
public slots:
	void playtimerexpired();
	void stoptimerexpired();
	void forwardtimerexpired();
	void backwardtimerexpired();
	void voluptimerexpired();
	void voldowntimerexpired();
private:

	RArray<TRemConCoreApiOperationId> iResponseQ; //response queue

	CRemConCoreApiTarget* iRemConCore; //the controller
	CRemConInterfaceSelector* iInterfaceSelector;
	
	QObject* receiver;
	
	QTimer* playtimer;
	QTimer* stoptimer;
	QTimer* forwardtimer;
	QTimer* backwardtimer;
	QTimer* voluptimer;
	QTimer* voldowntimer;
};

#endif /* SYMBIANMEDIAKEYS_H_ */
