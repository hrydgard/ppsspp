/*
 * Copyright (c) 2013 Antti Pohjola
 *
 */
//Adds mediakey support for Symbian (volume up/down)


#include <QKeyEvent>
#include <QApplication>
#include "SymbianMediakeys.h"

#define KTimeOut 80

#define KPlayButtonPress 0x101
#define KStopButtonPress 0x102
#define KForwardButtonPress 0x103
#define KBackwardButtonPress 0x104
#define KVolUpButtonPress 0x105
#define KVolDownButtonPress 0x106

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
    // TODO Auto-generated destructor stub
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
void SymbianMediaKeys::MrccatoCommand(TRemConCoreApiOperationId aOperationId, 
	TRemConCoreApiButtonAction aButtonAct){
	 QKeyEvent *event = NULL;
	TRequestStatus status;
       switch( aOperationId )
       {
       case ERemConCoreApiPausePlayFunction:
           {
           switch (aButtonAct)
               {
               case ERemConCoreApiButtonPress:
            	    event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KPlayButtonPress, Qt::NoModifier,
															KPlayButtonPress, KPlayButtonPress,Qt::NoModifier);
                   break;
               case ERemConCoreApiButtonRelease:
              	    event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyRelease, KPlayButtonPress, Qt::NoModifier,
           															KPlayButtonPress, KPlayButtonPress,Qt::NoModifier);
            	   // Play/Pause button released
                   break;
               case ERemConCoreApiButtonClick:
                   // Play/Pause button clicked
            	   playtimer->start(KTimeOut);
            	   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KPlayButtonPress, Qt::NoModifier,
            	 															KPlayButtonPress, KPlayButtonPress,Qt::NoModifier);
                   break;
               default:
                   // Play/Pause unknown action
                   break;
               }                               
           break;
           }   
         
       case ERemConCoreApiStop:
           {
           switch (aButtonAct)
               {
        	   case ERemConCoreApiButtonPress:
        		   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KStopButtonPress, Qt::NoModifier,
        				   KStopButtonPress, KStopButtonPress,Qt::NoModifier);
                  break;
               case ERemConCoreApiButtonRelease:
            	   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyRelease, KStopButtonPress, Qt::NoModifier,
            	           				   KStopButtonPress, KStopButtonPress,Qt::NoModifier);
                  break;
               case ERemConCoreApiButtonClick:
            	   stoptimer->start(KTimeOut);
            	   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KStopButtonPress, Qt::NoModifier,
            	           				   KStopButtonPress, KStopButtonPress,Qt::NoModifier);
                   break;   
               default:
                     
                break; 
               }
           break;
           }
       case ERemConCoreApiRewind:
           {
           switch (aButtonAct)
               {
        	   case ERemConCoreApiButtonPress:  
        		   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KBackwardButtonPress, Qt::NoModifier,
        		              			 KBackwardButtonPress, KBackwardButtonPress,Qt::NoModifier);
                  break;
               case ERemConCoreApiButtonRelease: 
         		   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyRelease, KBackwardButtonPress, Qt::NoModifier,
                		              			 KBackwardButtonPress, KBackwardButtonPress,Qt::NoModifier);
                  break;
               case ERemConCoreApiButtonClick:
            	   backwardtimer->start(KTimeOut);
         		   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KBackwardButtonPress, Qt::NoModifier,
                		              			 KBackwardButtonPress, KBackwardButtonPress,Qt::NoModifier);
               default:
                break; 
               }
           break;
           }    
       case ERemConCoreApiFastForward:
           {
           switch (aButtonAct)
               {
        	   case ERemConCoreApiButtonPress:
        		   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KForwardButtonPress, Qt::NoModifier,
        				   KForwardButtonPress, KForwardButtonPress,Qt::NoModifier);
                  break;
               case ERemConCoreApiButtonRelease:
            	   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyRelease, KForwardButtonPress, Qt::NoModifier,
            	        				   KForwardButtonPress, KForwardButtonPress,Qt::NoModifier);
                  break;
               case ERemConCoreApiButtonClick:
            	   forwardtimer->start(KTimeOut);
            	   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KForwardButtonPress, Qt::NoModifier,
            	        				   KForwardButtonPress, KForwardButtonPress,Qt::NoModifier);
               default:      
                break; 
               }
           break;
           }       
       case ERemConCoreApiVolumeUp:
           {   
           switch (aButtonAct)
               {
           	   case ERemConCoreApiButtonPress:
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KVolUpButtonPress, Qt::NoModifier,
						   KVolUpButtonPress, KVolUpButtonPress,Qt::NoModifier);
				  break;
			   case ERemConCoreApiButtonRelease:
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyRelease, KVolUpButtonPress, Qt::NoModifier,
				 						   KVolUpButtonPress, KVolUpButtonPress,Qt::NoModifier);
				  break;
			   case ERemConCoreApiButtonClick:
				   voluptimer->start(KTimeOut);
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KVolUpButtonPress, Qt::NoModifier,
				 						   KVolUpButtonPress, KVolUpButtonPress,Qt::NoModifier);
			   default:      
				break; 
               }
           break;
           }       
       case ERemConCoreApiVolumeDown:
           {
           switch (aButtonAct)
               {
          	   case ERemConCoreApiButtonPress:
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KVolDownButtonPress, Qt::NoModifier,
						   KVolDownButtonPress, KVolDownButtonPress,Qt::NoModifier);
				  break;
			   case ERemConCoreApiButtonRelease:
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyRelease, KVolDownButtonPress, Qt::NoModifier,
									   KVolDownButtonPress, KVolDownButtonPress,Qt::NoModifier);
				  break;
			   case ERemConCoreApiButtonClick:
				   voldowntimer->start(KTimeOut);
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KVolDownButtonPress, Qt::NoModifier,
									   KVolDownButtonPress, KVolDownButtonPress,Qt::NoModifier);
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
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KBackwardButtonPress, Qt::NoModifier,
						   KBackwardButtonPress, KBackwardButtonPress,Qt::NoModifier);
				  break;
			   case ERemConCoreApiButtonRelease:
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyRelease, KBackwardButtonPress, Qt::NoModifier,
						   KBackwardButtonPress, KBackwardButtonPress,Qt::NoModifier);
				  break;
			   case ERemConCoreApiButtonClick:
				   backwardtimer->start(KTimeOut);
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KBackwardButtonPress, Qt::NoModifier,
						   KBackwardButtonPress, KBackwardButtonPress,Qt::NoModifier);
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
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KForwardButtonPress, Qt::NoModifier,
						   KForwardButtonPress, KForwardButtonPress,Qt::NoModifier);
				  break;
			   case ERemConCoreApiButtonRelease:
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyRelease, KForwardButtonPress, Qt::NoModifier,
				  						   KForwardButtonPress, KForwardButtonPress,Qt::NoModifier);
				  break;
			   case ERemConCoreApiButtonClick:
				   forwardtimer->start(KTimeOut);
				   event =  QKeyEvent::createExtendedKeyEvent( QEvent::KeyPress, KForwardButtonPress, Qt::NoModifier,
				  						   KForwardButtonPress, KForwardButtonPress,Qt::NoModifier);
				   
			   default:      
				break; 
               }
           break;
           }
       	
       default:
           break;
       }
       //complete key event
       QCoreApplication::postEvent (receiver, event);
       CompleteMediaKeyEvent( aOperationId );
}

void SymbianMediaKeys::MrccatoPlay(TRemConCoreApiPlaybackSpeed aSpeed, 
	TRemConCoreApiButtonAction aButtonAct){
	
}

void SymbianMediaKeys::MrccatoTuneFunction(TBool aTwoPart, 
	TUint aMajorChannel, 
	TUint aMinorChannel,
	TRemConCoreApiButtonAction aButtonAct){
	
}

void SymbianMediaKeys::MrccatoSelectDiskFunction(TUint aDisk,
	TRemConCoreApiButtonAction aButtonAct){
	
}

void SymbianMediaKeys::MrccatoSelectAvInputFunction(TUint8 aAvInputSignalNumber,
	TRemConCoreApiButtonAction aButtonAct){
	
}

void SymbianMediaKeys::MrccatoSelectAudioInputFunction(TUint8 aAudioInputSignalNumber,
	TRemConCoreApiButtonAction aButtonAct){
	
}

void SymbianMediaKeys::CompleteMediaKeyEvent( TRemConCoreApiOperationId aOperationId ){
	if	( !IsActive() )
            {
            switch ( aOperationId )
                {
                case ERemConCoreApiVolumeUp:
                    {
                    iRemConCore->VolumeUpResponse( iStatus, KErrNone );
                    SetActive();
                    }
                    break;

                case ERemConCoreApiVolumeDown:
                    {
                    iRemConCore->VolumeDownResponse( iStatus, KErrNone );
                    SetActive();
                    }
                    break;
                case ERemConCoreApiPlay:
                    {
                    iRemConCore-> PlayResponse(iStatus, KErrNone);
                    SetActive();
                    }
                    break;
                case ERemConCoreApiStop:
                    {
                    iRemConCore->StopResponse(iStatus, KErrNone);
                    SetActive();
                    }
                    break;

                case ERemConCoreApiPause:
                    {
                    iRemConCore->PauseResponse(iStatus, KErrNone);
                    SetActive();
                    }
                    break;
                case ERemConCoreApiRewind:
                    {
                    iRemConCore->RewindResponse(iStatus, KErrNone);
                    SetActive();
                    }
                    break;
                case ERemConCoreApiFastForward:
                    {
                    iRemConCore->FastForwardResponse(iStatus, KErrNone);
                    SetActive();
                    }
                    break;
                case ERemConCoreApiForward:
                    {
                    iRemConCore->ForwardResponse( iStatus, KErrNone );
                    SetActive();
                    }
                    break;
                case ERemConCoreApiBackward:
                    {
                    iRemConCore->BackwardResponse(iStatus, KErrNone );
                    SetActive();
                    }
                    break;
                default:
                    {
                    }
                    break;
                }
            }
	else
		{
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
	QKeyEvent* event =  QKeyEvent::createExtendedKeyEvent(QEvent::KeyRelease, KPlayButtonPress, Qt::NoModifier,
			   KPlayButtonPress, KPlayButtonPress,Qt::NoModifier);
	QCoreApplication::postEvent (receiver, event);
}

void SymbianMediaKeys::stoptimerexpired(){
	stoptimer->stop();
	QKeyEvent* event =  QKeyEvent::createExtendedKeyEvent(QEvent::KeyRelease, KStopButtonPress, Qt::NoModifier,
			KStopButtonPress, KStopButtonPress,Qt::NoModifier);
	QCoreApplication::postEvent (receiver, event);
}
	
void SymbianMediaKeys::forwardtimerexpired(){
	forwardtimer->stop();
	QKeyEvent* event =  QKeyEvent::createExtendedKeyEvent(QEvent::KeyRelease, KForwardButtonPress, Qt::NoModifier,
			KForwardButtonPress, KForwardButtonPress,Qt::NoModifier);
	QCoreApplication::postEvent (receiver, event);
}
	
void SymbianMediaKeys::backwardtimerexpired(){
	backwardtimer->stop();
	QKeyEvent* event =  QKeyEvent::createExtendedKeyEvent(QEvent::KeyRelease, KBackwardButtonPress, Qt::NoModifier,
			KBackwardButtonPress, KBackwardButtonPress,Qt::NoModifier);
	QCoreApplication::postEvent (receiver, event);
}
	
void SymbianMediaKeys::voluptimerexpired(){
	voluptimer->stop();
	QKeyEvent* event =  QKeyEvent::createExtendedKeyEvent(QEvent::KeyRelease, KVolUpButtonPress, Qt::NoModifier,
			KVolUpButtonPress, KVolUpButtonPress,Qt::NoModifier);
	QCoreApplication::postEvent (receiver, event);
}

void SymbianMediaKeys::voldowntimerexpired(){
	voldowntimer->stop();
	QKeyEvent* event =  QKeyEvent::createExtendedKeyEvent(QEvent::KeyRelease, KVolDownButtonPress, Qt::NoModifier,
			KVolDownButtonPress, KVolDownButtonPress,Qt::NoModifier);
	QCoreApplication::postEvent (receiver, event);
}
