#ifndef OPENXR_PICO_H_
#define OPENXR_PICO_H_ 1


#include "openxr.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    char software_ver[6];
    char hardware_ver[3];
    char sn[18];
    char addr[6];
    char ndi_version[5];
} XrControllerInfo;

typedef enum
{
    XR_CONTROLLER_KEY_HOME = 0,
    XR_CONTROLLER_KEY_AX = 1,
    XR_CONTROLLER_KEY_BY= 2,
    XR_CONTROLLER_KEY_BACK = 3,
    XR_CONTROLLER_KEY_TRIGGER = 4,
    XR_CONTROLLER_KEY_VOL_UP = 5,
    XR_CONTROLLER_KEY_VOL_DOWN = 6,
    XR_CONTROLLER_KEY_ROCKER = 7,
    XR_CONTROLLER_KEY_GRIP = 8,
    XR_CONTROLLER_KEY_TOUCHPAD = 9,
    XR_CONTROLLER_KEY_LASTONE = 127,

    XR_CONTROLLER_TOUCH_AX = 128,
    XR_CONTROLLER_TOUCH_BY = 129,
    XR_CONTROLLER_TOUCH_ROCKER = 130,
    XR_CONTROLLER_TOUCH_TRIGGER = 131,
    XR_CONTROLLER_TOUCH_THUMB = 132,
    XR_CONTROLLER_TOUCH_LASTONE = 255
} XrControllerKeyMap;

enum xrt_device_eventtype
{
    XRT_DEVICE_CONNECTCHANGED = 0,
    XRT_DEVICE_MAIN_CHANGED = 1,
    XRT_DEVICE_VERSION = 2,
    XRT_DEVICE_SN = 3,
    XRT_DEVICE_BIND_STATUS = 4,
    XRT_STATION_STATUS = 5,
    XRT_DEVICE_IOBUSY = 6,
    XRT_DEVICE_OTASTAUS = 7,
    XRT_DEVICE_ID = 8,
//    XRT_DEVICE_OTASATAION_PROGRESS = 9,
//   XRT_DEVICE_OTASATAION_CODE = 10,
//    XRT_DEVICE_OTACONTROLLER_PROGRESS = 11,
//    XRT_DEVICE_OTACONTROLLER_CODE = 12,
//    XRT_DEVICE_OTA_SUCCESS = 13,
//    XRT_DEVICE_BLEMAC = 14,
    XRT_DEVICE_HANDNESS_CHANGED = 15,
    XRT_DEVICE_CHANNEL = 16,
    XRT_DEVICE_LOSSRATE = 17,
    XRT_DEVICE_THREAD_STARTED = 18
};

typedef struct XrRigidBodyPosef_ {
    XrPosef Pose;
    XrVector3f AngularVelocity;
    XrVector3f LinearVelocity;
    XrVector3f AngularAcceleration;
    XrVector3f LinearAcceleration;
    int64_t TimeInSeconds; //< Absolute time of this pose.
    int64_t PredictionInSeconds; //< Seconds this pose was predicted ahead.
} XrRigidBodyPosef;

typedef struct XrTracking_ {
    unsigned int Status;
    XrRigidBodyPosef ControllerLocalPose;
    XrRigidBodyPosef ControllerGlobalPose;
} XrTracking;

//pico add event:Seethrough
typedef struct XrEventDataSeethroughStateChanged {
        XrStructureType             type;
        const void* XR_MAY_ALIAS    next;
        int                         state;
} XrEventDataSeethroughStateChanged;

typedef struct XrEventDataKeyEvent{
        XrStructureType             type;
        const void* XR_MAY_ALIAS    next;
        int32_t                     repeat;
        int32_t                     keyCode;
        int8_t                      keyAction;
    }XrEventDataKeyEvent;
//pico add new device interface
typedef struct XrControllerEventChanged {
        XrStructureType            type;
        const void* XR_MAY_ALIAS    next;
        enum xrt_device_eventtype        eventtype;
        uint8_t                         controller;
        uint8_t                         status;
        uint8_t                     varying[400];
        uint16_t                         length;
} XrControllerEventChanged;

typedef struct XrEventDataHardIPDStateChanged {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    float                       ipd;
} XrEventDataHardIPDStateChanged;

typedef struct XrEventDataFoveationLevelChanged {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    int                        level;
} XrEventDataFoveationLevelChanged;

typedef struct XrEventDataFrustumChanged {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
} XrEventDataFrustumChanged;

typedef struct XrEventDataRenderTextureChanged {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    int                         width;
    int                         height;
} XrEventDataRenderTextureChanged;

typedef struct XrEventDataTargetFrameRateChanged {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    int                         frameRate;
} XrEventDataTargetFrameRateChanged;

typedef struct XrEventDataMrcStatusChanged {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    int                         mrcStatus;
} XrEventDataMrcStatusChanged;

//XR_TYPE_EVENT_REFRESH_RATE_CHANGE
typedef struct XrEventDataRefreshRateChanged {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    float                       refreshRate;
} XrEventDataRefreshRateChanged;

//pico add end


typedef enum XrTrackingMode
{
    XR_TRACKING_MODE_ROTATION = 0x1,
    XR_TRACKING_MODE_POSITION = 0x2,
    XR_TRACKING_MODE_EYE = 0x4
}XrTrackingMode;

typedef struct XrEyeTrackingData
{
    int32_t    leftEyePoseStatus;          //!< Bit field (pvrEyePoseStatus) indicating left eye pose status
    int32_t    rightEyePoseStatus;         //!< Bit field (pvrEyePoseStatus) indicating right eye pose status
    int32_t    combinedEyePoseStatus;      //!< Bit field (pvrEyePoseStatus) indicating combined eye pose status

    float      leftEyeGazePoint[3];        //!< Left Eye Gaze Point
    float      rightEyeGazePoint[3];       //!< Right Eye Gaze Point
    float      combinedEyeGazePoint[3];    //!< Combined Eye Gaze Point (HMD center-eye point)

    float      leftEyeGazeVector[3];       //!< Left Eye Gaze Point
    float      rightEyeGazeVector[3];      //!< Right Eye Gaze Point
    float      combinedEyeGazeVector[3];   //!< Comnbined Eye Gaze Vector (HMD center-eye point)

    float      leftEyeOpenness;            //!< Left eye value between 0.0 and 1.0 where 1.0 means fully open and 0.0 closed.
    float      rightEyeOpenness;           //!< Right eye value between 0.0 and 1.0 where 1.0 means fully open and 0.0 closed.

    float      leftEyePupilDilation;       //!< Left eye value in millimeters indicating the pupil dilation
    float      rightEyePupilDilation;      //!< Right eye value in millimeters indicating the pupil dilation

    float      leftEyePositionGuide[3];    //!< Position of the inner corner of the left eye in meters from the HMD center-eye coordinate system's origin.
    float      rightEyePositionGuide[3];   //!< Position of the inner corner of the right eye in meters from the HMD center-eye coordinate system's origin.
    float      foveatedGazeDirection[3];   //!< Position of the gaze direction in meters from the HMD center-eye coordinate system's origin.
    int32_t    foveatedGazeTrackingState;  //!< The current state of the foveatedGazeDirection signal.

}XrEyeTrackingData;

typedef XrResult (XRAPI_PTR *PFN_xrSetTrackingModePICO)(XrSession session, uint32_t trackingMode);
typedef XrResult (XRAPI_PTR *PFN_xrGetTrackingModePICO)(XrSession session, uint32_t *trackingMode);

typedef XrResult (XRAPI_PTR *PFN_xrGetEyeTrackingDataPICO)(XrSession session, XrEyeTrackingData *eyeTrackingData);

#ifndef XR_NO_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL xrSetTrackingModePICO(
    XrSession              session,
    uint32_t               trackingMode);

XRAPI_ATTR XrResult XRAPI_CALL xrGetTrackingModePICO(
                XrSession                            session,
        uint32_t *                      trackingMode);

XRAPI_ATTR XrResult XRAPI_CALL xrGetEyeTrackingDataPICO(
    XrSession              session,
    XrEyeTrackingData *    eyeTrackingData);

#endif

#define XR_PICO_singlepass_enable  1
#define XR_PICO_singlepass_enable_SPEC_VERSION 1
#define XR_PICO_SINGLEPASS_ENABLE_EXTENSION_NAME "XR_PICO_singlepass_enable"

#ifdef XR_USE_PLATFORM_ANDROID

//kevin extend for pass user created textures to runtime begin@{
#define XR_PICO_android_swapchain_ext_enable 1
#define XR_PICO_android_swapchain_ext_enable_SPEC_VERSION 1
#define XR_PICO_ANDROID_SWAPCHAIN_EXT_ENABLE_EXTENSION_NAME "XR_PICO_android_swapchain_ext_enable"
typedef struct XrSwapchainCreateInfoAndroidEXT {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    uint32_t                    imageNumExt;
    uint32_t*                   imagesExt;
} XrSwapchainCreateInfoAndroidEXT;
//kevin extend end@}

//peter extend for pass user ffr to runtime begin@{
typedef enum XrFoveationLevel{
    XR_FOVEATION_LEVEL_NONE = -1,
    XR_FOVEATION_LEVEL_LOW = 0,
    XR_FOVEATION_LEVEL_MID = 1,
    XR_FOVEATION_LEVEL_HIGH = 2,
    XR_FOVEATION_LEVEL_TOP_HIGH = 3
}XrFoveationLevel;

typedef enum XrFoveationType{
    XR_FOVEATION_LEVEL = 0,
    XR_FOVEATION_PARAMETERS = 1
}XrFoveationType;
typedef struct XrFoveationParametersEXT{
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    int                         textureIdCount;
    int*                        textureId;
    int*                        previousId;
    float                       focalPointX;
    float                       focalPointY;
    XrFoveationType             ffrType;
    float                       foveationGainX;
    float                       foveationGainY;
    float                       foveationArea;
    float                       foveationMinimum;
    XrFoveationLevel            level;
    int                         frameOffsetCount;
    float*                      frameOffset;
}XrFoveationParametersEXT;
//peter extend end@}

//peter extend for IPD in runtime begin@{
#define XR_PICO_ipd  1
#define XR_PICO_ipd_SPEC_VERSION 1
#define XR_PICO_IPD_EXTENSION_NAME "XR_PICO_ipd"

typedef XrResult (XRAPI_PTR *PFN_xrSetIPDPICO)(XrSession session, float distance);
typedef XrResult (XRAPI_PTR *PFN_xrGetIPDPICO)(XrSession session, float* ipd);
typedef XrResult (XRAPI_PTR *PFN_xrSetTrackingIPDEnabledPICO)(XrSession session, bool enable);
typedef XrResult (XRAPI_PTR *PFN_xrGetTrackingIPDEnabledPICO)(XrSession session, bool* enable);
typedef XrResult (XRAPI_PTR *PFN_xrGetEyeTrackingAutoIPDPICO)(XrSession session, float* autoIPD);

#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrSetIPDPICO(XrSession session, float distance);
XRAPI_ATTR XrResult XRAPI_CALL xrGetIPDPICO(XrSession session, float* ipd);
XRAPI_ATTR XrResult XRAPI_CALL xrSetTrackingIPDEnabledPICO(XrSession session, bool enable);
XRAPI_ATTR XrResult XRAPI_CALL xrGetTrackingIPDEnabledPICO(XrSession session, bool* enable);
XRAPI_ATTR XrResult XRAPI_CALL xrGetEyeTrackingAutoIPDPICO(XrSession session, float* autoIPD);
#endif
//peter extend for IPD end@}

//peter extend for stencilmesh in runtime begin @ {
#define XR_PICO_stencilmesh  1
#define XR_PICO_stencilmesh_SPEC_VERSION 1
#define XR_PICO_STENCILMESH_EXTENSION_NAME "XR_PICO_stencilmesh"

typedef XrResult (XRAPI_PTR *PFN_xrGetStencilmeshPICO)(
  XrSession                        session,
  int                              eye,
  int                              *vertsCount,
  int                              *indexCount,
  float                            **localVerts,
  unsigned int                     **localIndex
);

#ifndef XR_NO_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL xrGetStencilmeshPICO(
  XrSession                        session,
  int                              eye,
  int                              *vertsCount,
  int                              *indexCount,
  float                            **localVerts,
  unsigned int                     **localIndex
);
#endif
//peter extend for stencilmesh in runtime end @ }

// peter extend for frustum in runtime before @ {
#define XR_PICO_view_frustum_ext 1
#define XR_PICO_view_frustum_ext_SPEC_VERSION 1
#define XR_PICO_VIEW_FRUSTUM_EXT_EXTENSION_NAME "XR_PICO_view_frustum_ext"

struct XrViewFrustum
{
    float                     left;//!<?Left?Plane?of?Frustum
    float                     right;//!<?Right?Plane?of?Frustum
    float                     top;//!<?Top?Plane?of?Frustum
    float                     bottom;//!<?Bottom?Plane?of?Frustum
    float                     near;//!<?Near?Plane?of?Frustum
    float                     far;//!<?Far?Plane?of?Frustum?(Arbitrary)
    XrPosef                   frustumPose;
};

typedef XrResult (XRAPI_PTR *PFN_xrGetFrustumParametersPICO)(
        XrSession                              session,
        struct XrViewFrustum                   *pLeftFrustum,
        struct XrViewFrustum                   *pRightFrustum);

typedef XrResult (XRAPI_PTR *PFN_xrSetFrustumParametersPICO)(
        XrSession                              session,
        struct XrViewFrustum                   *pLeftFrustum,
        struct XrViewFrustum                   *pRightFrustum);

#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetFrustumParametersPICO(
        XrSession                              session,
        struct XrViewFrustum                   *pLeftFrustum,
        struct XrViewFrustum                   *pRightFrustum);

XRAPI_ATTR XrResult XRAPI_CALL xrSetFrustumParametersPICO(
        XrSession                              session,
        struct XrViewFrustum                   *pLeftFrustum,
        struct XrViewFrustum                   *pRightFrustum);

#endif
// peter extend for frustum in runtime end @ }

// peter extend for config in runtime before @ {
#define XR_PICO_configs_ext 1
#define XR_PICO_configs_ext_SPEC_VERSION 1
#define XR_PICO_CONFIGS_EXT_EXTENSION_NAME "XR_PICO_configs_ext"
enum ConfigsEXT
{
    RENDER_TEXTURE_WIDTH = 0,
    RENDER_TEXTURE_HEIGHT,
    SHOW_FPS,
    RUNTIME_LOG_LEVEL,
    PXRPLUGIN_LOG_LEVEL,
    UNITY_LOG_LEVEL,
    UNREAL_LOG_LEVEL,
    NATIVE_LOG_LEVEL,
    TARGET_FRAME_RATE,
    NECK_MODEL_X,
    NECK_MODEL_Y,
    NECK_MODEL_Z,
    DISPLAY_REFRESH_RATE,
    ENABLE_6DOF,
    CONTROLLER_TYPE,
    PHYSICAL_IPD,
    TO_DELTA_SENSOR_Y,
    GET_DISPLAY_RATE,
    FOVEATION_SUBSAMPLED_ENABLED = 18,
    TRACKING_ORIGIN_HEIGHT
};
enum ConfigsSetEXT
{
    UNREAL_VERSION = 0,
    TRACKING_ORIGIN,
    OPENGL_NOERROR,
    ENABLE_SIX_DOF,
    PRESENTATION_FLAG,
    ENABLE_CPT,
    PLATFORM,
    FOVEATION_LEVEL,
    SET_DISPLAY_RATE = 8,
    MRC_TEXTURE_ID = 9,
};
enum TrackingOrigin
{
    EYELEVEL = 0,
    FLOORLEVEL,
    STAGELEVEL
};
enum Platform
{
    UNITY = 0,
    UNREAL,
    NATIVE
};
struct ConfigsSetPICO
{
    char*     engineVersion;
    int       trackingOrigin;
    bool      noErrorFlag;
    bool      enableSixDof;
    bool      presentationFlag;
    int       platform;
    float     displayRate;
    uint64_t  mrcTextureId;
};
typedef XrResult (XRAPI_PTR *PFN_xrGetConfigPICO)(
        XrSession                              session,
        enum ConfigsEXT                        configIndex,
        float *                                configData);

typedef XrResult (XRAPI_PTR *PFN_xrGetConfigsPICO)(
        XrSession                              session,
        int  *                                 configCount,
        float **                               configArray);
typedef XrResult (XRAPI_PTR *PFN_xrSetConfigPICO) (
        XrSession                             session,
        enum ConfigsSetEXT                    configIndex,
        char *                                configData);
typedef XrResult (XRAPI_PTR *PFN_xrSetConfigsPICO) (
        XrSession                              session,
        struct ConfigsSetPICO *                configsData);
typedef XrResult (XRAPI_PTR *PFN_xrGetFoveationConfigPICO)(
        XrSession                              session,
        enum XrFoveationLevel                  level,
        float *                                gainX,
        float *                                gainY,
        float *                                area,
        float *                                minimum);
#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetConfigPICO(
        XrSession                              session,
        enum ConfigsEXT                        configIndex,
        float *                                configData);

XRAPI_ATTR XrResult XRAPI_CALL xrGetConfigsPICO(
        XrSession                              session,
        int  *                                 configCount,
        float **                               configArray);
XRAPI_ATTR XrResult XRAPI_CALL xrSetConfigPICO (
        XrSession                             session,
        enum ConfigsSetEXT                    configIndex,
        char *                                configData);
XRAPI_ATTR XrResult XRAPI_CALL xrSetConfigsPICO (
        XrSession                              session,
        struct ConfigsSetPICO *                configsData);
XRAPI_ATTR XrResult XRAPI_CALL  xrGetFoveationConfigPICO(
        XrSession                              session,
        enum XrFoveationLevel                  level,
        float *                                gainX,
        float *                                gainY,
        float *                                area,
        float *                                minimum);
#endif
// peter extend for config in runtime end @ }

// peter add reset sensor begin @ {
#define XR_PICO_reset_sensor  1
#define XR_PICO_reset_sensor_SPEC_VERSION 1
#define XR_PICO_RESET_SENSOR_EXTENSION_NAME "XR_PICO_reset_sensor"

typedef enum XrResetSensorOption {
    XR_RESET_POSITION = 0,
    XR_RESET_ORIENTATION = 1,
    XR_RESET_ORIENTATION_Y_ONLY= 2,
    XR_RESET_ALL
} XrResetSensorOption;

typedef XrResult (XRAPI_PTR *PFN_xrResetSensorPICO)(XrSession session, XrResetSensorOption option);

#ifndef XR_NO_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL xrResetSensorPICO(
        XrSession                                   session,
        XrResetSensorOption                         option);

#endif
// reset sensor end @ }


//add setMrcPose and getMrcPose {
#define XR_PICO_android_getMrcPose_function_ext_enable  1
#define XR_PICO_android_MrcPose_function_ext_enable_SPEC_VERSION 1
#define XR_PICO_ANDROID_MRCPOSE_FUNCTION_EXT_ENABLE_EXTENSION_NAME "XR_PICO_android_MrcPose_function_ext_enable"

typedef XrResult (XRAPI_PTR *PFN_xrSetMrcPose)(
        XrSession                                 session,
        float                                     x,
        float                                     y,
        float                                     z,
        float                                     w,
        float                                     px,
        float                                     py,
        float                                     pz);

typedef XrResult (XRAPI_PTR *PFN_xrGetMrcPose)(
        XrSession                                 session,
        float *                                     x,
        float *                                     y,
        float *                                     z,
        float *                                     w,
        float *                                     px,
        float *                                     py,
        float *                                     pz);

#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrSetMrcPose(
        XrSession                                 session,
        float                                     x,
        float                                     y,
        float                                     z,
        float                                     w,
        float                                     px,
        float                                     py,
        float                                     pz);

XRAPI_ATTR XrResult XRAPI_CALL xrGetMrcPose(
        XrSession                                   session,
        float *                                     x,
        float *                                     y,
        float *                                     z,
        float *                                     w,
        float *                                     px,
        float *                                     py,
        float *                                     pz);

#endif

//add setMrcGlobalPose and getMrcLocalPose }

// peter add boundary api begin @ {
#define XR_PICO_boundary_ext  1
#define XR_PICO_boundary_ext_SPEC_VERSION 1
#define XR_PICO_BOUNDARY_EXT_EXTENSION_NAME "XR_PICO_boundary_ext"

typedef enum xrFuncitonName {
    XR_SET_SEETHROUGH_VISIBLE = 0,
    XR_SET_GUARDIANSYSTEM_DISABLE,
    XR_RESUME_GUARDIANSYSTEM_FOR_STS,
    XR_PAUSE_GUARDIANSYSTEM_FOR_STS,
    XR_SHUTDOWN_SDK_GUARDIANSYSTEM,
    XR_GET_CAMERA_DATA_EXT,
    XR_START_SDK_BOUNDARY,
    XR_SET_CONTROLLER_POSITION,  //unused
    XR_START_CAMERA_PREVIEW,
    XR_GET_ROOM_MODE_STATE,
    XR_DISABLE_BOUNDARY,
    XR_SET_MONO_MODE,
    XR_GET_BOUNDARY_CONFIGURED,
    XR_GET_BOUNDARY_ENABLED,
    XR_SET_BOUNDARY_VISIBLE,
    XR_SET_SEETHROUGH_BACKGROUND,
    XR_GET_BOUNDARY_VISIBLE,
} xrFuncitonName;

typedef XrResult (XRAPI_PTR *PFN_xrInvokeFunctionsPICO)(
        XrSession                                 session,
        xrFuncitonName                            name,
        void *                                    input,
        unsigned int                              size_in,
        void **                                   output,
        unsigned int                              size_out);

typedef XrResult (XRAPI_PTR *PFN_xrSetControllerPositionPICO)(
        XrSession                                 session,
        float                                     x,
        float                                     y,
        float                                     z,
        float                                     w,
        float                                     px,
        float                                     py,
        float                                     pz,
        int                                       hand,
        bool                                      valid,
        int                                       keyEvent);

typedef XrResult (XRAPI_PTR *PFN_xrBoundaryTestNodePICO)(
        XrSession                                 session,
        int                                       node,
        bool                                      isPlayArea,
        bool *                                    pisTriggering,
        float *                                   pclosestDistance,
        float *                                   ppx,
        float *                                   ppy,
        float *                                   ppz,
        float *                                   pnx,
        float *                                   pny,
        float *                                   pnz,
        int *                                     ret);

typedef XrResult (XRAPI_PTR *PFN_xrBoundaryTestPointPICO)(
        XrSession                                 session,
        float                                     x,
        float                                     y,
        float                                     z,
        bool                                      isPlayArea,
        bool *                                    pisTriggering,
        float *                                   pclosestDistance,
        float *                                   ppx,
        float *                                   ppy,
        float *                                   ppz,
        float *                                   pnx,
        float *                                   pny,
        float *                                   pnz,
        int *                                     ret);

typedef XrResult (XRAPI_PTR *PFN_xrGetBoundaryGeometryPICO)(
        XrSession                                 session,
        float **                                  outPointsFloat,
        bool                                      isPlayArea,
        int *                                     ret);

typedef XrResult (XRAPI_PTR *PFN_xrGetBoundaryDimensionsPICO)(
        XrSession                                 session,
        float *                                   x,
        float *                                   y,
        float *                                   z,
        bool                                      isPlayArea,
        int *                                     ret);

typedef XrResult (XRAPI_PTR *PFN_xrGetSeeThroughDataPICO)(
        XrSession                                 session,
        uint8_t *                                 leftEye,
        uint8_t *                                 rightEye,
        uint32_t *                                width,
        uint32_t *                                height,
        uint32_t *                                exposure,
        int64_t *                                 start_of_exposure_ts,
        int *                                     ret);

#ifndef XR_NO_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL xrInvokeFunctionsPICO(
        XrSession                                   session,
        xrFuncitonName                              name,
        void *                                      input,
        unsigned int                                size_in,
        void **                                     output,
        unsigned int                                size_out);

XRAPI_ATTR XrResult XRAPI_CALL xrSetControllerPositionPICO(
        XrSession                                   session,
        float                                       x,
        float                                       y,
        float                                       z,
        float                                       w,
        float                                       px,
        float                                       py,
        float                                       pz,
        int                                         hand,
        bool                                        valid,
        int                                         keyEvent);

XRAPI_ATTR XrResult XRAPI_CALL xrBoundaryTestNodePICO(
        XrSession                                   session,
        int                                         node,
        bool                                        isPlayArea,
        bool *                                      pisTriggering,
        float *                                     pclosestDistance,
        float *                                     ppx,
        float *                                     ppy,
        float *                                     ppz,
        float *                                     pnx,
        float *                                     pny,
        float *                                     pnz,
        int *                                       ret);

XRAPI_ATTR XrResult XRAPI_CALL xrBoundaryTestPointPICO(
        XrSession                                   session,
        float                                       x,
        float                                       y,
        float                                       z,
        bool                                        isPlayArea,
        bool *                                      pisTriggering,
        float *                                     pclosestDistance,
        float *                                     ppx,
        float *                                     ppy,
        float *                                     ppz,
        float *                                     pnx,
        float *                                     pny,
        float *                                     pnz,
        int *                                       ret);

XRAPI_ATTR XrResult XRAPI_CALL xrGetBoundaryGeometryPICO(
        XrSession                                   session,
        float **                                    outPointsFloat,
        bool                                        isPlayArea,
        int *                                       ret);

XRAPI_ATTR XrResult XRAPI_CALL xrGetBoundaryDimensionsPICO(
        XrSession                                   session,
        float *                                     x,
        float *                                     y,
        float *                                     z,
        bool                                        isPlayArea,
        int *                                       ret);

XRAPI_ATTR XrResult XRAPI_CALL xrGetSeeThroughDataPICO(
        XrSession                                   session,
        uint8_t *                                   leftEye,
        uint8_t *                                   rightEye,
        uint32_t *                                  width,
        uint32_t *                                  height,
        uint32_t *                                  exposure,
        int64_t *                                   start_of_exposure_ts,
        int *                                       ret);

#endif
// boundary api end @ }

// peter add performance settings begin @ {
#define XR_PICO_performance_settings 1
#define XR_PICO_performance_settings_SPEC_VERSION 1
#define XR_PICO_PERFORMANCE_SETTINGS_EXTENSION_NAME "XR_PICO_performance_settings"

typedef XrResult (XRAPI_PTR *PFN_xrSetPerformanceLevelPICO)(
    XrSession                                   session,
    XrPerfSettingsDomainEXT                     domain,
    int                                         level);

typedef XrResult (XRAPI_PTR *PFN_xrGetPerformanceLevelPICO)(
    XrSession                                   session,
    XrPerfSettingsDomainEXT                     domain,
    int *                                       level);

#ifndef XR_NO_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL xrSetPerformanceLevelPICO(
        XrSession                               session,
        XrPerfSettingsDomainEXT                 domain,
        int                                     level);

XRAPI_ATTR XrResult XRAPI_CALL xrGetPerformanceLevelPICO(
        XrSession                               session,
        XrPerfSettingsDomainEXT                 domain,
        int *                                   level);
#endif
// performance settings end @ }

//gaojian extend for pass surfaceview to runtime for display begin@{
#define XR_PICO_android_create_instance_ext_enable 1
#define XR_PICO_android_create_instance_ext_enable_SPEC_VERSION 1
#define XR_PICO_ANDROID_CREATE_INSTANCE_EXT_ENABLE_EXTENSION_NAME "XR_PICO_android_create_instance_ext_enable"
typedef struct XrInstanceCreateInfoAndroidPICOEXT {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    struct ANativeWindow*                   nativeWindow;
    jobject*                    surfaceView;
} XrInstanceCreateInfoAndroidPICOEXT;
//gaojian extend end@}

//gaojian extend for get headpose when call xrLocateViews begin@{
#define XR_PICO_view_state_ext_enable 1
#define XR_PICO_view_state_ext_enable_SPEC_VERSION 1
#define XR_PICO_VIEW_STATE_EXT_ENABLE_EXTENSION_NAME "XR_PICO_view_state_ext_enable"
typedef struct XrViewStatePICOEXT {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrPosef                     headpose;
    int32_t             poseStatus;             //!< Bit field (sxrTrackingMode) indicating pose status
    uint64_t            poseTimeStampNs;        //!< Time stamp in which the head pose was generated (nanoseconds)
    uint64_t            poseFetchTimeNs;        //!< Time stamp when this pose was retrieved
    uint64_t            expectedDisplayTimeNs;  //!< Expected time when this pose should be on screen (nanoseconds)
    int                 gsIndex;
    XrVector3f          linear_velocity;
    XrVector3f          angular_velocity;
    XrVector3f          linear_acceleration;
    XrVector3f          angular_acceleration;
} XrViewStatePICOEXT;
//gaojian extend end@}

#define XR_PICO_frame_end_info_ext  1
#define XR_PICO_frame_end_info_ext_SPEC_VERSION 1
#define XR_PICO_FRAME_END_INFO_EXT_EXTENSION_NAME "XR_PICO_frame_end_info_ext"
typedef struct XrFrameEndInfoEXT
{
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    // Alex FFR begin @{
    uint32_t                    enableFoveation;
    XrFoveationParametersEXT    foveationParametersEXT;
    //Alex FFR end @}
    //gaojian extend begin@{
    uint32_t            useHeadposeExt;
    XrPosef             headpose;
    int32_t             poseStatus;             //!< Bit field (sxrTrackingMode) indicating pose status
    uint64_t            poseTimeStampNs;        //!< Time stamp in which the head pose was generated (nanoseconds)
    uint64_t            poseFetchTimeNs;        //!< Time stamp when this pose was retrieved
    uint64_t            expectedDisplayTimeNs;  //!< Expected time when this pose should be on screen (nanoseconds)
    int                 gsIndex;
    //gaojian extend end@}
    float               depth;
}XrFrameEndInfoEXT;

//gaojian extend  begin@{
#define XR_PICO_session_begin_info_ext_enable 1
#define XR_PICO_session_begin_info_ext_enable_SPEC_VERSION 1
#define XR_PICO_SESSION_BEGIN_INFO_EXT_ENABLE_EXTENSION_NAME "XR_PICO_session_begin_info_ext_enable"
enum XrColorSpace{    
	colorSpaceLinear = 0,    
	colorSpaceSRGB = 1,
};
typedef struct XrSessionBeginInfoEXT {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    uint32_t                    enableSinglePass;
    enum XrColorSpace                colorSpace;
} XrSessionBeginInfoEXT;
//gaojian extend end@}

#define XR_PICO_foveation_image_ext_enable 1
#define XR_PICO_foveation_image_ext_enable_SPEC_VERSION 1
#define XR_PICO_FOVEATION_IMAGE_EXT_ENABLE_EXTENSION_NAME "XR_PICO_foveation_image_ext_enable"
typedef XrResult (XRAPI_PTR *PFN_xrGetFoveationImagePICO)(XrSession session, XrSwapchain swapchain, int eye, uint64_t* foveationImage, uint32_t* width, uint32_t* height);
#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetFoveationImagePICO(XrSession session, XrSwapchain swapchain, int eye, uint64_t* foveationImage, uint32_t* width, uint32_t* height);
#endif

// mrc begin
#define XR_PICO_mrc_pose_ext_enable 1
#define XR_PICO_mrc_pose_ext_enable_SPEC_VERSION 1
#define XR_PICO_MRC_POSE_EXT_ENABLE_EXTENSION_NAME "XR_PICO_mrc_pose_ext_enable"
typedef XrResult (XRAPI_PTR *PFN_xrGetMrcPosePICO)(XrSession session, float *x, float *y, float *z, float *w, float *px, float *py, float *pz);
typedef XrResult (XRAPI_PTR *PFN_xrSetMrcPosePICO)(XrSession session, float x, float y, float z,float w,float px, float py, float pz);

#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetMrcPosePICO(XrSession session, float *x, float *y, float *z, float *w, float *px, float *py, float *pz);
XRAPI_ATTR XrResult XRAPI_CALL xrSetMrcPosePICO(XrSession session, float x, float y, float z,float w,float px, float py, float pz);
#endif
// mrc end

//berton extend for getting controller functions begin@{ pico add new property
#define XR_PICO_android_controller_function_ext_enable 1
#define XR_PICO_android_controller_function_ext_enable_SPEC_VERSION 1
#define XR_PICO_ANDROID_CONTROLLER_FUNCTION_EXT_ENABLE_EXTENSION_NAME "XR_PICO_android_controller_function_ext_enable"
//cmc ext function ,not use from 2021/07
typedef XrResult (XRAPI_PTR *PFN_xrSetEngineVersionPico)(XrInstance instance,const char* version);
typedef XrResult (XRAPI_PTR *PFN_xrSetControllerEventCallbackPico)(XrInstance instance,bool enable_controller_callback);
typedef XrResult (XRAPI_PTR *PFN_xrResetControllerSensorPico)(XrInstance instance,int controllerHandle);
typedef XrResult (XRAPI_PTR *PFN_xrGetConnectDeviceMacPico)(XrInstance instance,char* mac);
typedef XrResult (XRAPI_PTR *PFN_xrStartCVControllerThreadPico)(XrInstance instance,int headSensorState, int handSensorState);
typedef XrResult (XRAPI_PTR *PFN_xrStopCVControllerThreadPico)(XrInstance instance,int headSensorState, int handSensorState);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerAngularVelocityStatePico)(XrInstance instance,int controllerHandle,float* data);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerAccelerationStatePico)(XrInstance instance,int controllerHandle,float *data);
typedef XrResult (XRAPI_PTR *PFN_xrResetHeadSensorForControllerPico)(XrInstance instance);
typedef XrResult (XRAPI_PTR *PFN_xrSetIsEnbleHomeKeyPico)(XrInstance instance,bool isEnable);
typedef XrResult (XRAPI_PTR *PFN_xrGetHeadSensorDataPico)(XrInstance instance,float* data);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerSensorDataPredictPico)(XrInstance instance,int controllerHandle, float headSensorData[], float predictTime,float* data);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerLinearVelocityStatePico)(XrInstance instance,int controllerHandle,float* data);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerSensorDataPico)(XrInstance instance,int controllerHandle, float headSensorData[],float* data);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerFixedSensorStatePico)(XrInstance instance,int controllerHandle,float* data);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerTouchValuePico)(XrInstance instance,int controllerSerialNum,int length,int* value);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerGripValuePico)(XrInstance instance,int controllerSerialNum,int *tripvalue);
//cmc ext function ,not use from 2021/07
//inputmanager
typedef XrResult (XRAPI_PTR *PFN_xrSetMainControllerHandlePico)(XrInstance instance,int controllerHandle);
typedef XrResult (XRAPI_PTR *PFN_xrGetMainControllerHandlePico)(XrInstance instance,int* controllerHandle);
typedef XrResult (XRAPI_PTR *PFN_xrGetControllerConnectionStatePico)(XrInstance instance,uint8_t controllerhandle,uint8_t* status);
typedef XrResult (XRAPI_PTR *PFN_xrGetPhyControllerInfoPico)(XrInstance instance,int device,XrControllerInfo * controllerinfo);
typedef XrResult (XRAPI_PTR *PFN_xrVibrateControllerPico)(XrInstance instance,float strength ,int time,int controllerHandle);
typedef XrResult (XRAPI_PTR *PFN_xrSetPhyControllerEnterPairingPico)(XrInstance instance,int device);
typedef XrResult (XRAPI_PTR *PFN_xrSetPhyControllerStopPairingPico)(XrInstance instance,int device);
typedef XrResult (XRAPI_PTR *PFN_xrSetPhyControllerUpgradePico)(XrInstance instance,int devicetype,int rule,char* station_path_by_char,char* controller_path_by_char);
typedef XrResult (XRAPI_PTR *PFN_xrSetPhyControllerUnbindPico)(XrInstance instance,int device);
typedef XrResult (XRAPI_PTR *PFN_xrSetPhyControllerEnableKeyPico)(XrInstance instance,bool isEnable,XrControllerKeyMap Key);
#ifndef XR_NO_PROTOTYPES
//cmc ext function ,not use from 2021/07
XRAPI_ATTR XrResult XRAPI_CALL xrSetEngineVersionPico(XrInstance instance,const char* version);
XRAPI_ATTR XrResult XRAPI_CALL xrSetControllerEventCallbackPico(XrInstance instance,bool enable_controller_callback);
XRAPI_ATTR XrResult XRAPI_CALL xrResetControllerSensorPico(XrInstance instance,int controllerHandle);
XRAPI_ATTR XrResult XRAPI_CALL xrGetConnectDeviceMacPico(XrInstance instance,char* mac);
XRAPI_ATTR XrResult XRAPI_CALL xrStartCVControllerThreadPico(XrInstance instance,int headSensorState, int handSensorState);
XRAPI_ATTR XrResult XRAPI_CALL xrStopCVControllerThreadPico(XrInstance instance,int headSensorState, int handSensorState);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerAngularVelocityStatePico(XrInstance instance,int controllerHandle,float* data);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerAccelerationStatePico(XrInstance instance,int controllerHandle,float *data);
XRAPI_ATTR XrResult XRAPI_CALL xrResetHeadSensorForControllerPico(XrInstance instance);
XRAPI_ATTR XrResult XRAPI_CALL xrSetIsEnbleHomeKeyPico(XrInstance instance,bool isEnable);
XRAPI_ATTR XrResult XRAPI_CALL xrGetHeadSensorDataPico(XrInstance instance,float* data);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerSensorDataPredictPico(XrInstance instance,int controllerHandle, float headSensorData[], float predictTime,float* data);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerLinearVelocityStatePico(XrInstance instance,int controllerHandle,float* data);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerSensorDataPico(XrInstance instance,int controllerHandle, float headSensorData[],float* data);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerFixedSensorStatePico(XrInstance instance,int controllerHandle,float* data);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerTouchValuePico(XrInstance instance,int controllerSerialNum,int length,int* value);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerGripValuePico(XrInstance instance,int controllerSerialNum,int *tripvalue);
//cmc ext function ,not use from 2021/07

//inputmanager
XRAPI_ATTR XrResult XRAPI_CALL xrSetMainControllerHandlePico(XrInstance instance,int controllerHandle);
XRAPI_ATTR XrResult XRAPI_CALL xrGetMainControllerHandlePico(XrInstance instance,int* controllerHandle);
XRAPI_ATTR XrResult XRAPI_CALL xrGetControllerConnectionStatePico(
        XrInstance instance,uint8_t controllerhandle,uint8_t* status);
XRAPI_ATTR XrResult XRAPI_CALL xrGetPhyControllerInfoPico(XrInstance instance,int device,XrControllerInfo * controllerinfo);
XRAPI_ATTR XrResult XRAPI_CALL xrVibrateControllerPico(XrInstance instance,float strength ,int time,int controllerHandle);
XRAPI_ATTR XrResult XRAPI_CALL xrSetPhyControllerEnterPairingPico(XrInstance instance,int device);
XRAPI_ATTR XrResult XRAPI_CALL xrSetPhyControllerStopPairingPico(XrInstance instance,int device);
XRAPI_ATTR XrResult XRAPI_CALL xrSetPhyControllerUpgradePico(XrInstance instance,int devicetype,int rule,char* station_path_by_char,char* controller_path_by_char);
XRAPI_ATTR XrResult XRAPI_CALL xrSetPhyControllerUnbindPico(XrInstance instance,int device);
XRAPI_ATTR XrResult XRAPI_CALL xrSetPhyControllerEnableKeyPico(XrInstance instance,bool isEnable,XrControllerKeyMap Key);
#endif//berton extend end@} pico end
#endif



#ifdef __cplusplus
}
#endif

#endif // OPENXR_PICO_H_
