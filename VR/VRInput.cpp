#include "VRInput.h"
#include <string.h>
#include <sys/time.h>

//OpenXR
XrPath leftHandPath;
XrPath rightHandPath;
XrAction handPoseLeftAction;
XrAction handPoseRightAction;
XrAction indexLeftAction;
XrAction indexRightAction;
XrAction menuAction;
XrAction buttonAAction;
XrAction buttonBAction;
XrAction buttonXAction;
XrAction buttonYAction;
XrAction gripLeftAction;
XrAction gripRightAction;
XrAction moveOnLeftJoystickAction;
XrAction moveOnRightJoystickAction;
XrAction thumbstickLeftClickAction;
XrAction thumbstickRightClickAction;
XrAction vibrateLeftFeedback;
XrAction vibrateRightFeedback;
XrActionSet runningActionSet;
XrSpace leftControllerAimSpace = XR_NULL_HANDLE;
XrSpace rightControllerAimSpace = XR_NULL_HANDLE;
int actionsAttached = 0;
int inputInitialized = 0;
int useSimpleProfile = 0;

int in_vrEventTime = 0;
double lastframetime = 0;

uint32_t lButtons = 0;
uint32_t rButtons = 0;
XrActionStateVector2f moveJoystickState[2];

float radians(float deg) {
    return (deg * M_PI) / 180.0;
}

unsigned long sys_timeBase = 0;
int milliseconds(void) {
	struct timeval tp;

	gettimeofday(&tp, NULL);

	if (!sys_timeBase) {
		sys_timeBase = tp.tv_sec;
		return tp.tv_usec/1000;
	}

	return (tp.tv_sec - sys_timeBase)*1000 + tp.tv_usec/1000;
}

#ifndef EPSILON
#define EPSILON 0.001f
#endif

XrVector3f normalizeVec(XrVector3f vec) {
    float xxyyzz = vec.x*vec.x + vec.y*vec.y + vec.z*vec.z;

    XrVector3f result;
    float invLength = 1.0f / sqrtf(xxyyzz);
    result.x = vec.x * invLength;
    result.y = vec.y * invLength;
    result.z = vec.z * invLength;
    return result;
}

void GetAnglesFromVectors(const XrVector3f forward, const XrVector3f right, const XrVector3f up, vec3_t angles) {
    float sr, sp, sy, cr, cp, cy;

    sp = -forward.z;

    float cp_x_cy = forward.x;
    float cp_x_sy = forward.y;
    float cp_x_sr = -right.z;
    float cp_x_cr = up.z;

    float yaw = atan2(cp_x_sy, cp_x_cy);
    float roll = atan2(cp_x_sr, cp_x_cr);

    cy = cos(yaw);
    sy = sin(yaw);
    cr = cos(roll);
    sr = sin(roll);

    if (fabs(cy) > EPSILON) {
        cp = cp_x_cy / cy;
    } else if (fabs(sy) > EPSILON) {
        cp = cp_x_sy / sy;
    } else if (fabs(sr) > EPSILON) {
        cp = cp_x_sr / sr;
    } else if (fabs(cr) > EPSILON) {
        cp = cp_x_cr / cr;
    } else {
        cp = cos(asin(sp));
    }

    float pitch = atan2(sp, cp);

    angles[0] = pitch / (M_PI*2.f / 360.f);
    angles[1] = yaw / (M_PI*2.f / 360.f);
    angles[2] = roll / (M_PI*2.f / 360.f);
}

void QuatToYawPitchRoll(XrQuaternionf q, vec3_t rotation, vec3_t out) {

    ovrMatrix4f mat = ovrMatrix4f_CreateFromQuaternion( &q );

    if (rotation[0] != 0.0f || rotation[1] != 0.0f || rotation[2] != 0.0f) {
        ovrMatrix4f rot = ovrMatrix4f_CreateRotation(radians(rotation[0]), radians(rotation[1]), radians(rotation[2]));
        mat = ovrMatrix4f_Multiply(&mat, &rot);
    }

    XrVector4f v1 = {0, 0, -1, 0};
    XrVector4f v2 = {1, 0, 0, 0};
    XrVector4f v3 = {0, 1, 0, 0};

    XrVector4f forwardInVRSpace = XrVector4f_MultiplyMatrix4f(&mat, &v1);
    XrVector4f rightInVRSpace = XrVector4f_MultiplyMatrix4f(&mat, &v2);
    XrVector4f upInVRSpace = XrVector4f_MultiplyMatrix4f(&mat, &v3);

    XrVector3f forward = {-forwardInVRSpace.z, -forwardInVRSpace.x, forwardInVRSpace.y};
    XrVector3f right = {-rightInVRSpace.z, -rightInVRSpace.x, rightInVRSpace.y};
    XrVector3f up = {-upInVRSpace.z, -upInVRSpace.x, upInVRSpace.y};

    XrVector3f forwardNormal = normalizeVec(forward);
    XrVector3f rightNormal = normalizeVec(right);
    XrVector3f upNormal = normalizeVec(up);

    GetAnglesFromVectors(forwardNormal, rightNormal, upNormal, out);
}

//0 = left, 1 = right
float vibration_channel_duration[2] = {0.0f, 0.0f};
float vibration_channel_intensity[2] = {0.0f, 0.0f};

void VR_Vibrate( int duration, int chan, float intensity ) {
    for (int i = 0; i < 2; ++i) {
        int channel = (i + 1) & chan;
        if (channel) {
            if (vibration_channel_duration[channel-1] > 0.0f)
                return;

            if (vibration_channel_duration[channel-1] == -1.0f && duration != 0.0f)
                return;

            vibration_channel_duration[channel-1] = duration;
            vibration_channel_intensity[channel-1] = intensity;
        }
    }
}


void VR_processHaptics() {
    float lastFrameTime = 0.0f;
    float timestamp = (float)(milliseconds( ));
    float frametime = timestamp - lastFrameTime;
    lastFrameTime = timestamp;

    for (int i = 0; i < 2; ++i) {
        if (vibration_channel_duration[i] > 0.0f ||
            vibration_channel_duration[i] == -1.0f) {

            // fire haptics using output action
            XrHapticVibration vibration = {};
            vibration.type = XR_TYPE_HAPTIC_VIBRATION;
            vibration.next = NULL;
            vibration.amplitude = vibration_channel_intensity[i];
            vibration.duration = ToXrTime(vibration_channel_duration[i]);
            vibration.frequency = 3000;
            XrHapticActionInfo hapticActionInfo = {};
            hapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
            hapticActionInfo.next = NULL;
            hapticActionInfo.action = i == 0 ? vibrateLeftFeedback : vibrateRightFeedback;
            OXR(xrApplyHapticFeedback(VR_GetEngine()->appState.Session, &hapticActionInfo, (const XrHapticBaseHeader*)&vibration));

            if (vibration_channel_duration[i] != -1.0f) {
                vibration_channel_duration[i] -= frametime;

                if (vibration_channel_duration[i] < 0.0f) {
                    vibration_channel_duration[i] = 0.0f;
                    vibration_channel_intensity[i] = 0.0f;
                }
            }
        } else {
            // Stop haptics
            XrHapticActionInfo hapticActionInfo = {};
            hapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
            hapticActionInfo.next = NULL;
            hapticActionInfo.action = i == 0 ? vibrateLeftFeedback : vibrateRightFeedback;
            OXR(xrStopHapticFeedback(VR_GetEngine()->appState.Session, &hapticActionInfo));
        }
    }
}

XrSpace CreateActionSpace(XrAction poseAction, XrPath subactionPath) {
    XrActionSpaceCreateInfo asci = {};
    asci.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
    asci.action = poseAction;
    asci.poseInActionSpace.orientation.w = 1.0f;
    asci.subactionPath = subactionPath;
    XrSpace actionSpace = XR_NULL_HANDLE;
    OXR(xrCreateActionSpace(VR_GetEngine()->appState.Session, &asci, &actionSpace));
    return actionSpace;
}

XrActionSuggestedBinding ActionSuggestedBinding(XrAction action, const char* bindingString) {
    XrActionSuggestedBinding asb;
    asb.action = action;
    XrPath bindingPath;
    OXR(xrStringToPath(VR_GetEngine()->appState.Instance, bindingString, &bindingPath));
    asb.binding = bindingPath;
    return asb;
}

XrActionSet CreateActionSet(int priority, const char* name, const char* localizedName) {
    XrActionSetCreateInfo asci = {};
    asci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    asci.next = NULL;
    asci.priority = priority;
    strcpy(asci.actionSetName, name);
    strcpy(asci.localizedActionSetName, localizedName);
    XrActionSet actionSet = XR_NULL_HANDLE;
    OXR(xrCreateActionSet(VR_GetEngine()->appState.Instance, &asci, &actionSet));
    return actionSet;
}

XrAction CreateAction(
        XrActionSet actionSet,
        XrActionType type,
        const char* actionName,
        const char* localizedName,
        int countSubactionPaths,
        XrPath* subactionPaths) {

    XrActionCreateInfo aci = {};
    aci.type = XR_TYPE_ACTION_CREATE_INFO;
    aci.next = NULL;
    aci.actionType = type;
    if (countSubactionPaths > 0) {
        aci.countSubactionPaths = countSubactionPaths;
        aci.subactionPaths = subactionPaths;
    }
    strcpy(aci.actionName, actionName);
    strcpy(aci.localizedActionName, localizedName ? localizedName : actionName);
    XrAction action = XR_NULL_HANDLE;
    OXR(xrCreateAction(actionSet, &aci, &action));
    return action;
}

int ActionPoseIsActive(XrAction action, XrPath subactionPath) {
    XrActionStateGetInfo getInfo = {};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;
    getInfo.subactionPath = subactionPath;

    XrActionStatePose state = {};
    state.type = XR_TYPE_ACTION_STATE_POSE;
    OXR(xrGetActionStatePose(VR_GetEngine()->appState.Session, &getInfo, &state));
    return state.isActive != XR_FALSE;
}

XrActionStateFloat GetActionStateFloat(XrAction action) {
    XrActionStateGetInfo getInfo = {};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;

    XrActionStateFloat state = {};
    state.type = XR_TYPE_ACTION_STATE_FLOAT;

    OXR(xrGetActionStateFloat(VR_GetEngine()->appState.Session, &getInfo, &state));
    return state;
}

XrActionStateBoolean GetActionStateBoolean(XrAction action) {
    XrActionStateGetInfo getInfo = {};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;

    XrActionStateBoolean state = {};
    state.type = XR_TYPE_ACTION_STATE_BOOLEAN;

    OXR(xrGetActionStateBoolean(VR_GetEngine()->appState.Session, &getInfo, &state));
    return state;
}

XrActionStateVector2f GetActionStateVector2(XrAction action) {
    XrActionStateGetInfo getInfo = {};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;

    XrActionStateVector2f state = {};
    state.type = XR_TYPE_ACTION_STATE_VECTOR2F;

    OXR(xrGetActionStateVector2f(VR_GetEngine()->appState.Session, &getInfo, &state));
    return state;
}

void IN_VRInit( engine_t *engine ) {
    if (inputInitialized)
        return;

    // Actions
    runningActionSet = CreateActionSet(1, "running_action_set", "Action Set used on main loop");
    indexLeftAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "index_left", "Index left", 0, NULL);
    indexRightAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "index_right", "Index right", 0, NULL);
    menuAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_action", "Menu", 0, NULL);
    buttonAAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a", "Button A", 0, NULL);
    buttonBAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b", "Button B", 0, NULL);
    buttonXAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x", "Button X", 0, NULL);
    buttonYAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y", "Button Y", 0, NULL);
    gripLeftAction = CreateAction(runningActionSet, XR_ACTION_TYPE_FLOAT_INPUT, "grip_left", "Grip left", 0, NULL);
    gripRightAction = CreateAction(runningActionSet, XR_ACTION_TYPE_FLOAT_INPUT, "grip_right", "Grip right", 0, NULL);
    moveOnLeftJoystickAction = CreateAction(runningActionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "move_on_left_joy", "Move on left Joy", 0, NULL);
    moveOnRightJoystickAction = CreateAction(runningActionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "move_on_right_joy", "Move on right Joy", 0, NULL);
    thumbstickLeftClickAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_left", "Thumbstick left", 0, NULL);
    thumbstickRightClickAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_right", "Thumbstick right", 0, NULL);
    vibrateLeftFeedback = CreateAction(runningActionSet, XR_ACTION_TYPE_VIBRATION_OUTPUT, "vibrate_left_feedback", "Vibrate Left Controller Feedback", 0, NULL);
    vibrateRightFeedback = CreateAction(runningActionSet, XR_ACTION_TYPE_VIBRATION_OUTPUT, "vibrate_right_feedback", "Vibrate Right Controller Feedback", 0, NULL);

    OXR(xrStringToPath(engine->appState.Instance, "/user/hand/left", &leftHandPath));
    OXR(xrStringToPath(engine->appState.Instance, "/user/hand/right", &rightHandPath));
    handPoseLeftAction = CreateAction(runningActionSet, XR_ACTION_TYPE_POSE_INPUT, "hand_pose_left", NULL, 1, &leftHandPath);
    handPoseRightAction = CreateAction(runningActionSet, XR_ACTION_TYPE_POSE_INPUT, "hand_pose_right", NULL, 1, &rightHandPath);

    XrPath interactionProfilePath = XR_NULL_PATH;
    XrPath interactionProfilePathTouch = XR_NULL_PATH;
    XrPath interactionProfilePathKHRSimple = XR_NULL_PATH;

    OXR(xrStringToPath(engine->appState.Instance, "/interaction_profiles/oculus/touch_controller", &interactionProfilePathTouch));
    OXR(xrStringToPath(engine->appState.Instance, "/interaction_profiles/khr/simple_controller", &interactionProfilePathKHRSimple));

    // Toggle this to force simple as a first choice, otherwise use it as a last resort
    if (useSimpleProfile) {
        ALOGV("xrSuggestInteractionProfileBindings found bindings for Khronos SIMPLE controller");
        interactionProfilePath = interactionProfilePathKHRSimple;
    } else {
        // Query Set
        XrActionSet queryActionSet = CreateActionSet(1, "query_action_set", "Action Set used to query device caps");
        XrAction dummyAction = CreateAction(queryActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "dummy_action", "Dummy Action", 0, NULL);

        // Map bindings
        XrActionSuggestedBinding bindings[1];
        int currBinding = 0;
        bindings[currBinding++] = ActionSuggestedBinding(dummyAction, "/user/hand/right/input/system/click");

        XrInteractionProfileSuggestedBinding suggestedBindings = {};
        suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggestedBindings.next = NULL;
        suggestedBindings.suggestedBindings = bindings;
        suggestedBindings.countSuggestedBindings = currBinding;

        // Try all
        suggestedBindings.interactionProfile = interactionProfilePathTouch;
        XrResult suggestTouchResult = xrSuggestInteractionProfileBindings(engine->appState.Instance, &suggestedBindings);
        OXR(suggestTouchResult);

        if (XR_SUCCESS == suggestTouchResult) {
            ALOGV("xrSuggestInteractionProfileBindings found bindings for QUEST controller");
            interactionProfilePath = interactionProfilePathTouch;
        }

        if (interactionProfilePath == XR_NULL_PATH) {
            // Simple as a fallback
            bindings[0] = ActionSuggestedBinding(dummyAction, "/user/hand/right/input/select/click");
            suggestedBindings.interactionProfile = interactionProfilePathKHRSimple;
            XrResult suggestKHRSimpleResult = xrSuggestInteractionProfileBindings(engine->appState.Instance, &suggestedBindings);
            OXR(suggestKHRSimpleResult);
            if (XR_SUCCESS == suggestKHRSimpleResult) {
                ALOGV("xrSuggestInteractionProfileBindings found bindings for Khronos SIMPLE controller");
                interactionProfilePath = interactionProfilePathKHRSimple;
            } else {
                ALOGE("xrSuggestInteractionProfileBindings did NOT find any bindings.");
                exit(1);
            }
        }
    }

    // Action creation
    {
        // Map bindings
        XrActionSuggestedBinding bindings[32]; // large enough for all profiles
        int currBinding = 0;

        {
            if (interactionProfilePath == interactionProfilePathTouch) {
                bindings[currBinding++] = ActionSuggestedBinding(indexLeftAction, "/user/hand/left/input/trigger");
                bindings[currBinding++] = ActionSuggestedBinding(indexRightAction, "/user/hand/right/input/trigger");
                bindings[currBinding++] = ActionSuggestedBinding(menuAction, "/user/hand/left/input/menu/click");
                bindings[currBinding++] = ActionSuggestedBinding(buttonXAction, "/user/hand/left/input/x/click");
                bindings[currBinding++] = ActionSuggestedBinding(buttonYAction, "/user/hand/left/input/y/click");
                bindings[currBinding++] = ActionSuggestedBinding(buttonAAction, "/user/hand/right/input/a/click");
                bindings[currBinding++] = ActionSuggestedBinding(buttonBAction, "/user/hand/right/input/b/click");
                bindings[currBinding++] = ActionSuggestedBinding(gripLeftAction, "/user/hand/left/input/squeeze/value");
                bindings[currBinding++] = ActionSuggestedBinding(gripRightAction, "/user/hand/right/input/squeeze/value");
                bindings[currBinding++] = ActionSuggestedBinding(moveOnLeftJoystickAction, "/user/hand/left/input/thumbstick");
                bindings[currBinding++] = ActionSuggestedBinding(moveOnRightJoystickAction, "/user/hand/right/input/thumbstick");
                bindings[currBinding++] = ActionSuggestedBinding(thumbstickLeftClickAction, "/user/hand/left/input/thumbstick/click");
                bindings[currBinding++] = ActionSuggestedBinding(thumbstickRightClickAction, "/user/hand/right/input/thumbstick/click");
                bindings[currBinding++] = ActionSuggestedBinding(vibrateLeftFeedback, "/user/hand/left/output/haptic");
                bindings[currBinding++] = ActionSuggestedBinding(vibrateRightFeedback, "/user/hand/right/output/haptic");
                bindings[currBinding++] = ActionSuggestedBinding(handPoseLeftAction, "/user/hand/left/input/aim/pose");
                bindings[currBinding++] = ActionSuggestedBinding(handPoseRightAction, "/user/hand/right/input/aim/pose");
            }

            if (interactionProfilePath == interactionProfilePathKHRSimple) {
                bindings[currBinding++] = ActionSuggestedBinding(indexLeftAction, "/user/hand/left/input/select/click");
                bindings[currBinding++] = ActionSuggestedBinding(indexRightAction, "/user/hand/right/input/select/click");
                bindings[currBinding++] = ActionSuggestedBinding(buttonAAction, "/user/hand/left/input/menu/click");
                bindings[currBinding++] = ActionSuggestedBinding(buttonXAction, "/user/hand/right/input/menu/click");
                bindings[currBinding++] = ActionSuggestedBinding(vibrateLeftFeedback, "/user/hand/left/output/haptic");
                bindings[currBinding++] = ActionSuggestedBinding(vibrateRightFeedback, "/user/hand/right/output/haptic");
                bindings[currBinding++] = ActionSuggestedBinding(handPoseLeftAction, "/user/hand/left/input/aim/pose");
                bindings[currBinding++] = ActionSuggestedBinding(handPoseRightAction, "/user/hand/right/input/aim/pose");
            }
        }

        XrInteractionProfileSuggestedBinding suggestedBindings = {};
        suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggestedBindings.next = NULL;
        suggestedBindings.interactionProfile = interactionProfilePath;
        suggestedBindings.suggestedBindings = bindings;
        suggestedBindings.countSuggestedBindings = currBinding;
        OXR(xrSuggestInteractionProfileBindings(engine->appState.Instance, &suggestedBindings));

        // Enumerate actions
        XrPath actionPathsBuffer[32];
        char stringBuffer[256];
        XrAction actionsToEnumerate[] = {
                indexLeftAction,
                indexRightAction,
                menuAction,
                buttonAAction,
                buttonBAction,
                buttonXAction,
                buttonYAction,
                gripLeftAction,
                gripRightAction,
                moveOnLeftJoystickAction,
                moveOnRightJoystickAction,
                thumbstickLeftClickAction,
                thumbstickRightClickAction,
                vibrateLeftFeedback,
                vibrateRightFeedback,
                handPoseLeftAction,
                handPoseRightAction
        };
        for (size_t i = 0; i < sizeof(actionsToEnumerate) / sizeof(actionsToEnumerate[0]); ++i) {
            XrBoundSourcesForActionEnumerateInfo enumerateInfo = {};
            enumerateInfo.type = XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO;
            enumerateInfo.next = NULL;
            enumerateInfo.action = actionsToEnumerate[i];

            // Get Count
            uint32_t countOutput = 0;
            OXR(xrEnumerateBoundSourcesForAction(
                    engine->appState.Session, &enumerateInfo, 0 /* request size */, &countOutput, NULL));
            ALOGV(
                    "xrEnumerateBoundSourcesForAction action=%lld count=%u",
                    (long long)enumerateInfo.action,
                    countOutput);

            if (countOutput < 32) {
                OXR(xrEnumerateBoundSourcesForAction(
                        engine->appState.Session, &enumerateInfo, 32, &countOutput, actionPathsBuffer));
                for (uint32_t a = 0; a < countOutput; ++a) {
                    XrInputSourceLocalizedNameGetInfo nameGetInfo = {};
                    nameGetInfo.type = XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO;
                    nameGetInfo.next = NULL;
                    nameGetInfo.sourcePath = actionPathsBuffer[a];
                    nameGetInfo.whichComponents = XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT |
                                                  XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT |
                                                  XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;

                    uint32_t stringCount = 0u;
                    OXR(xrGetInputSourceLocalizedName(
                            engine->appState.Session, &nameGetInfo, 0, &stringCount, NULL));
                    if (stringCount < 256) {
                        OXR(xrGetInputSourceLocalizedName(
                                engine->appState.Session, &nameGetInfo, 256, &stringCount, stringBuffer));
                        char pathStr[256];
                        uint32_t strLen = 0;
                        OXR(xrPathToString(
                                engine->appState.Instance,
                                actionPathsBuffer[a],
                                (uint32_t)sizeof(pathStr),
                                &strLen,
                                pathStr));
                        ALOGV(
                                "  -> path = %lld `%s` -> `%s`",
                                (long long)actionPathsBuffer[a],
                                pathStr,
                                stringBuffer);
                    }
                }
            }
        }
    }
    inputInitialized = 1;
}

void IN_VRInputFrame( engine_t* engine ) {
    // Attach to session
    if (!actionsAttached) {
        XrSessionActionSetsAttachInfo attachInfo = {};
        attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
        attachInfo.next = NULL;
        attachInfo.countActionSets = 1;
        attachInfo.actionSets = &runningActionSet;
        OXR(xrAttachSessionActionSets(engine->appState.Session, &attachInfo));
        actionsAttached = 1;
    }

    // sync action data
    XrActiveActionSet activeActionSet = {};
    activeActionSet.actionSet = runningActionSet;
    activeActionSet.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo syncInfo = {};
    syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
    syncInfo.next = NULL;
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    OXR(xrSyncActions(engine->appState.Session, &syncInfo));

    // query input action states
    XrActionStateGetInfo getInfo = {};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.next = NULL;
    getInfo.subactionPath = XR_NULL_PATH;

    VR_processHaptics();

    if (leftControllerAimSpace == XR_NULL_HANDLE) {
        leftControllerAimSpace = CreateActionSpace(handPoseLeftAction, leftHandPath);
    }
    if (rightControllerAimSpace == XR_NULL_HANDLE) {
        rightControllerAimSpace = CreateActionSpace(handPoseRightAction, rightHandPath);
    }

    //button mapping
    lButtons = 0;
    if (GetActionStateBoolean(menuAction).currentState) lButtons |= ovrButton_Enter;
    if (GetActionStateBoolean(buttonXAction).currentState) lButtons |= ovrButton_X;
    if (GetActionStateBoolean(buttonYAction).currentState) lButtons |= ovrButton_Y;
    if (GetActionStateBoolean(indexLeftAction).currentState) lButtons |= ovrButton_Trigger;
    if (GetActionStateFloat(gripLeftAction).currentState > 0.5f) lButtons |= ovrButton_GripTrigger;
    if (GetActionStateBoolean(thumbstickLeftClickAction).currentState) lButtons |= ovrButton_LThumb;
    rButtons = 0;
    if (GetActionStateBoolean(buttonAAction).currentState) rButtons |= ovrButton_A;
    if (GetActionStateBoolean(buttonBAction).currentState) rButtons |= ovrButton_B;
    if (GetActionStateBoolean(indexRightAction).currentState) rButtons |= ovrButton_Trigger;
    if (GetActionStateFloat(gripRightAction).currentState > 0.5f) rButtons |= ovrButton_GripTrigger;
    if (GetActionStateBoolean(thumbstickRightClickAction).currentState) rButtons |= ovrButton_RThumb;

    //thumbstick
    moveJoystickState[0] = GetActionStateVector2(moveOnLeftJoystickAction);
    moveJoystickState[1] = GetActionStateVector2(moveOnRightJoystickAction);
    if (moveJoystickState[0].currentState.x > 0.5) lButtons |= ovrButton_Right;
    if (moveJoystickState[0].currentState.x < -0.5) lButtons |= ovrButton_Left;
    if (moveJoystickState[0].currentState.y > 0.5) lButtons |= ovrButton_Up;
    if (moveJoystickState[0].currentState.y < -0.5) lButtons |= ovrButton_Down;
    if (moveJoystickState[1].currentState.x > 0.5) rButtons |= ovrButton_Right;
    if (moveJoystickState[1].currentState.x < -0.5) rButtons |= ovrButton_Left;
    if (moveJoystickState[1].currentState.y > 0.5) rButtons |= ovrButton_Up;
    if (moveJoystickState[1].currentState.y < -0.5) rButtons |= ovrButton_Down;

	lastframetime = in_vrEventTime;
	in_vrEventTime = milliseconds( );
}


uint32_t IN_VRGetButtonState( int controllerIndex ) {
	switch (controllerIndex) {
		case 0:
			return lButtons;
		case 1:
			return rButtons;
		default:
			return 0;
	}
}

XrVector2f IN_VRGetJoystickState( int controllerIndex ) {
	return moveJoystickState[controllerIndex].currentState;
}
