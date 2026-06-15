package org.ppsspp.ppsspp;

import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

class PPSSPPAccessibilityDelegate extends View.AccessibilityDelegate {
	private static final String TAG = "PPSSPPAccessibility";
	static final int ACTION_RELEASE_ALL = 0x01020001;
	private static final long REFRESH_INTERVAL_MS = 500;

	private final View host;
	private final Handler handler = new Handler(Looper.getMainLooper());
	private final Provider provider = new Provider();
	private final Runnable refreshRunnable = new Runnable() {
		@Override
		public void run() {
			refresh();
			if (enabled) {
				handler.postDelayed(this, REFRESH_INTERVAL_MS);
			}
		}
	};

	private boolean enabled;
	private boolean hardwareControllerConnected;
	private long lastVersion = -1;
	private String lastHeading = "";
	private float nativeWidth = 1.0f;
	private float nativeHeight = 1.0f;
	private int accessibilityFocusedId = View.NO_ID;
	private int hoveredId = View.NO_ID;
	private final List<Node> nodes = new ArrayList<>();
	private final Map<Integer, Node> nodesById = new HashMap<>();
	private final List<Node> lastNonEmptyNodes = new ArrayList<>();

	PPSSPPAccessibilityDelegate(View host) {
		this.host = host;
		host.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
		host.setOnHoverListener((view, event) -> handleHover(event));
	}

	void setEnabled(boolean enabled) {
		if (this.enabled == enabled) {
			return;
		}
		this.enabled = enabled;
		handler.removeCallbacks(refreshRunnable);
		if (enabled) {
			lastVersion = -1;
			refresh();
			handler.postDelayed(refreshRunnable, REFRESH_INTERVAL_MS);
		} else {
			clearNodes();
		}
	}

	void setHardwareControllerConnected(boolean connected) {
		if (hardwareControllerConnected != connected) {
			hardwareControllerConnected = connected;
			NativeApp.releaseAccessibilityInputs();
			lastVersion = -1;
			refresh();
		}
	}

	void resetInputs() {
		NativeApp.releaseAccessibilityInputs();
	}

	@Override
	public AccessibilityNodeProvider getAccessibilityNodeProvider(View host) {
		return provider;
	}

	@Override
	public void onInitializeAccessibilityNodeInfo(View host, AccessibilityNodeInfo info) {
		super.onInitializeAccessibilityNodeInfo(host, info);
		info.setClassName(View.class.getName());
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
			info.setScreenReaderFocusable(false);
		}
		for (Node node : nodes) {
			info.addChild(host, node.id);
		}
		info.addAction(new AccessibilityNodeInfo.AccessibilityAction(ACTION_RELEASE_ALL, host.getContext().getString(R.string.accessibility_release_all)));
	}

	@Override
	public boolean performAccessibilityAction(View host, int action, Bundle args) {
		if (action == ACTION_RELEASE_ALL) {
			NativeApp.releaseAccessibilityInputs();
			host.announceForAccessibility(host.getContext().getString(R.string.accessibility_all_released));
			return true;
		}
		return super.performAccessibilityAction(host, action, args);
	}

	private void refresh() {
		if (!enabled) {
			return;
		}
		long version = NativeApp.getAccessibilitySnapshotVersion();
		if (version == lastVersion) {
			return;
		}
		try {
			String snapshotJson = NativeApp.getAccessibilitySnapshotJson();
			JSONObject root = new JSONObject(snapshotJson);
			nativeWidth = (float)root.optDouble("width", 1.0);
			nativeHeight = (float)root.optDouble("height", 1.0);
			Node previousSelectedTab = selectedTab(nodes);
			nodes.clear();
			nodesById.clear();
			String heading = "";
			String firstLabel = "";
			JSONArray array = root.getJSONArray("nodes");
			for (int i = 0; i < array.length(); ++i) {
				Node node = new Node(array.getJSONObject(i));
				if (hardwareControllerConnected && "gamepad_control".equals(node.role)) {
					continue;
				}
				nodes.add(node);
				nodesById.put(node.id, node);
				if (firstLabel.isEmpty() && !node.label.isEmpty()) {
					firstLabel = node.label;
				}
				if (heading.isEmpty() && "heading".equals(node.role)) {
					heading = node.label;
				}
			}
			lastVersion = version;
			if (!nodesById.containsKey(hoveredId)) {
				updateHoveredId(View.NO_ID);
			}
			sendEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED, View.NO_ID);
			Node selectedTab = selectedTab(nodes);
			if (previousSelectedTab != null && selectedTab != null && !previousSelectedTab.label.equals(selectedTab.label)) {
				sendEvent(AccessibilityEvent.TYPE_VIEW_SELECTED, selectedTab.id);
			}
			if (!heading.isEmpty() && !heading.equals(lastHeading)) {
				lastHeading = heading;
				host.announceForAccessibility(heading);
			}
			if (!nodes.isEmpty()) {
				if (heading.isEmpty() && isSubstantialTreeReplacement(lastNonEmptyNodes, nodes)) {
					accessibilityFocusedId = View.NO_ID;
					sendEvent(AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED, View.NO_ID);
					if (!firstLabel.isEmpty()) {
						host.announceForAccessibility(firstLabel);
					}
				}
				lastNonEmptyNodes.clear();
				lastNonEmptyNodes.addAll(nodes);
			}
		} catch (Exception e) {
			Log.e(TAG, "Failed to load accessibility snapshot version=" + version, e);
			clearNodes();
		}
	}

	private Node selectedTab(List<Node> candidates) {
		for (Node node : candidates) {
			if ("tab".equals(node.role) && node.selected) {
				return node;
			}
		}
		return null;
	}

	private boolean isSubstantialTreeReplacement(List<Node> previous, List<Node> current) {
		if (previous.isEmpty() || current.isEmpty()) {
			return false;
		}
		int matchingNodes = 0;
		for (Node oldNode : previous) {
			for (Node newNode : current) {
				if (oldNode.label.equals(newNode.label) && oldNode.role.equals(newNode.role)) {
					++matchingNodes;
					break;
				}
			}
		}
		return matchingNodes * 2 < Math.min(previous.size(), current.size());
	}

	private void clearNodes() {
		updateHoveredId(View.NO_ID);
		nodes.clear();
		nodesById.clear();
		accessibilityFocusedId = View.NO_ID;
		lastVersion = -1;
		sendEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED, View.NO_ID);
	}

	private void sendEvent(int type, int virtualId) {
		if (!host.isAttachedToWindow()) {
			return;
		}
		AccessibilityEvent event = AccessibilityEvent.obtain(type);
		event.setPackageName(host.getContext().getPackageName());
		if (virtualId == View.NO_ID) {
			event.setSource(host);
		} else {
			event.setSource(host, virtualId);
			Node node = nodesById.get(virtualId);
			if (node != null) {
				event.setClassName(node.className());
				event.setContentDescription(node.label);
			}
		}
		if (type == AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED) {
			event.setContentChangeTypes(AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE);
		}
		if (host.getParent() != null) {
			host.getParent().requestSendAccessibilityEvent(host, event);
		}
	}

	private Rect boundsFor(Node node) {
		float sx = host.getWidth() / Math.max(1.0f, nativeWidth);
		float sy = host.getHeight() / Math.max(1.0f, nativeHeight);
		return new Rect(Math.round(node.left * sx), Math.round(node.top * sy),
			Math.round(node.right * sx), Math.round(node.bottom * sy));
	}

	private boolean handleHover(MotionEvent event) {
		if (!enabled) {
			return false;
		}
		if (event.getActionMasked() == MotionEvent.ACTION_HOVER_EXIT) {
			updateHoveredId(View.NO_ID);
			return true;
		}
		if (event.getActionMasked() != MotionEvent.ACTION_HOVER_ENTER && event.getActionMasked() != MotionEvent.ACTION_HOVER_MOVE) {
			return false;
		}
		int newHoveredId = View.NO_ID;
		for (Node node : nodes) {
			if (boundsFor(node).contains(Math.round(event.getX()), Math.round(event.getY()))) {
				newHoveredId = node.id;
				break;
			}
		}
		updateHoveredId(newHoveredId);
		return newHoveredId != View.NO_ID;
	}

	private void updateHoveredId(int newHoveredId) {
		if (hoveredId == newHoveredId) {
			return;
		}
		if (hoveredId != View.NO_ID) {
			sendEvent(AccessibilityEvent.TYPE_VIEW_HOVER_EXIT, hoveredId);
		}
		hoveredId = newHoveredId;
		if (hoveredId != View.NO_ID) {
			sendEvent(AccessibilityEvent.TYPE_VIEW_HOVER_ENTER, hoveredId);
		}
	}

	private Rect screenBoundsFor(Node node) {
		Rect bounds = boundsFor(node);
		int[] location = new int[2];
		host.getLocationOnScreen(location);
		bounds.offset(location[0], location[1]);
		return bounds;
	}

	private void tapKey(int keyCode) {
		NativeApp.keyDown(NativeApp.DEVICE_ID_DEFAULT, keyCode, false);
		NativeApp.keyUp(NativeApp.DEVICE_ID_DEFAULT, keyCode);
	}

	private class Provider extends AccessibilityNodeProvider {
		@Override
		public AccessibilityNodeInfo createAccessibilityNodeInfo(int virtualViewId) {
			if (virtualViewId == View.NO_ID) {
				AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(host);
				PPSSPPAccessibilityDelegate.this.onInitializeAccessibilityNodeInfo(host, info);
				return info;
			}
			Node node = nodesById.get(virtualViewId);
			if (node == null) {
				return null;
			}
			AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain();
			info.setSource(host, node.id);
			info.setParent(host);
			info.setPackageName(host.getContext().getPackageName());
			info.setClassName(node.className());
			info.setContentDescription(node.label);
			info.setBoundsInParent(boundsFor(node));
			info.setBoundsInScreen(screenBoundsFor(node));
			info.setEnabled(node.enabled);
			info.setFocusable(true);
			info.setVisibleToUser(true);
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
				info.setScreenReaderFocusable(true);
			}
			info.setClickable(node.clickable);
			info.setLongClickable(node.longClickable);
			if ("checkbox".equals(node.role) || "radio".equals(node.role)) {
				info.setCheckable(true);
				info.setChecked(node.checked);
			}
			if ("tab".equals(node.role)) {
				info.setSelected(node.selected);
			}
			if ("heading".equals(node.role)) {
				info.setHeading(true);
			}
			if (node.clickable) {
				info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_CLICK);
			}
			if (node.longClickable) {
				info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_LONG_CLICK);
			}
			if ("slider".equals(node.role)) {
				info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD);
				info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD);
			}
			if (node.id == accessibilityFocusedId) {
				info.setAccessibilityFocused(true);
				info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_ACCESSIBILITY_FOCUS);
			} else {
				info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_ACCESSIBILITY_FOCUS);
			}
			return info;
		}

		@Override
		public boolean performAction(int virtualViewId, int action, Bundle args) {
			Node node = nodesById.get(virtualViewId);
			if (node == null) {
				return false;
			}
			if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS) {
				accessibilityFocusedId = virtualViewId;
				NativeApp.focusAccessibilityElement(virtualViewId);
				sendEvent(AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED, virtualViewId);
				return true;
			}
			if (action == AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) {
				accessibilityFocusedId = View.NO_ID;
				sendEvent(AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED, virtualViewId);
				return true;
			}
			if (action == AccessibilityNodeInfo.ACTION_CLICK) {
				return NativeApp.performAccessibilityClick(virtualViewId, false);
			}
			if (action == AccessibilityNodeInfo.ACTION_LONG_CLICK) {
				boolean handled = NativeApp.performAccessibilityClick(virtualViewId, true);
				if (handled) {
					host.announceForAccessibility(host.getContext().getString(R.string.accessibility_hold_toggled, node.label));
				}
				return handled;
			}
			if (action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD) {
				tapKey(KeyEvent.KEYCODE_DPAD_RIGHT);
				return true;
			}
			if (action == AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD) {
				tapKey(KeyEvent.KEYCODE_DPAD_LEFT);
				return true;
			}
			return false;
		}

		@Override
		public AccessibilityNodeInfo findFocus(int focus) {
			if (focus == AccessibilityNodeInfo.FOCUS_ACCESSIBILITY && accessibilityFocusedId != View.NO_ID) {
				return createAccessibilityNodeInfo(accessibilityFocusedId);
			}
			return null;
		}
	}

	private static class Node {
		final int id;
		final String label;
		final String role;
		final float left;
		final float top;
		final float right;
		final float bottom;
		final boolean enabled;
		final boolean checked;
		final boolean selected;
		final boolean clickable;
		final boolean longClickable;

		Node(JSONObject json) {
			id = json.optInt("id", -1);
			label = json.optString("label", "");
			role = json.optString("role", "text");
			left = (float)json.optDouble("left", 0.0);
			top = (float)json.optDouble("top", 0.0);
			right = (float)json.optDouble("right", 0.0);
			bottom = (float)json.optDouble("bottom", 0.0);
			enabled = json.optBoolean("enabled", true);
			checked = json.optBoolean("checked", false);
			selected = json.optBoolean("selected", false);
			clickable = json.optBoolean("clickable", false);
			longClickable = json.optBoolean("longClickable", false);
		}

		String className() {
			switch (role) {
			case "button":
			case "choice":
			case "gamepad_control":
				return android.widget.Button.class.getName();
			case "checkbox":
				return android.widget.CheckBox.class.getName();
			case "radio":
				return android.widget.RadioButton.class.getName();
			case "tab":
				return "android.app.ActionBar$Tab";
			case "slider":
				return android.widget.SeekBar.class.getName();
			case "text_field":
				return android.widget.EditText.class.getName();
			case "image":
				return android.widget.ImageView.class.getName();
			default:
				return android.widget.TextView.class.getName();
			}
		}
	}
}
