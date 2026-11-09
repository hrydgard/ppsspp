package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.ContentProviderClient;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.StatFs;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.DocumentsContract;
import android.system.Os;
import android.system.StructStatVfs;
import android.text.TextUtils;
import android.util.Log;

import androidx.annotation.Keep;
import androidx.annotation.RequiresApi;
import androidx.documentfile.provider.DocumentFile;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

public class ContentUri {
	private static final String TAG = "PpssppActivity";  // don't want to add yet another tag

	// Matches the enum in AndroidStorage.h.
	private static final int STORAGE_ERROR_SUCCESS = 0;
	private static final int STORAGE_ERROR_UNKNOWN = -1;
	private static final int STORAGE_ERROR_NOT_FOUND = -2;
	private static final int STORAGE_ERROR_DISK_FULL = -3;
	private static final int STORAGE_ERROR_ALREADY_EXISTS = -4;

	// So the native side can tell a missing file or a full disk from other failures.
	private static int errorFromException(Exception e) {
		if (e instanceof java.io.FileNotFoundException) {
			return STORAGE_ERROR_NOT_FOUND;
		}
		final String message = e.getMessage();
		if (message != null && (message.contains("ENOSPC") || message.contains("No space left"))) {
			return STORAGE_ERROR_DISK_FULL;
		}
		return STORAGE_ERROR_UNKNOWN;
	}

	private static final String EXTERNAL_STORAGE_AUTHORITY = "com.android.externalstorage.documents";

	// The name a document has. For shared storage it's the last part of the document ID ("primary:PSP/SAVEDATA"),
	// other providers' IDs can be anything, so those are asked.
	private static String documentName(Context context, Uri documentUri) {
		if (EXTERNAL_STORAGE_AUTHORITY.equals(documentUri.getAuthority())) {
			final String id = DocumentsContract.getDocumentId(documentUri);
			final int slash = id.lastIndexOf('/');
			return id.substring(slash >= 0 ? slash + 1 : id.indexOf(':') + 1);
		}
		try (Cursor c = context.getContentResolver().query(documentUri, new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
			return c != null && c.moveToFirst() ? c.getString(0) : null;
		}
	}

	// Whether the provider made a renamed copy because the name was taken. It answers that with "name (1)", or
	// "name (1).ext" for a file (FileUtils.buildUniqueFile), and before that replaces the characters FAT doesn't
	// allow with '_' (buildValidFatFilename). The replacement alone isn't a conflict, the name was free.
	private static boolean isRenamedCopy(String requested, String actual) {
		if (actual == null || actual.equals(requested)) {
			return false;
		}
		final String sanitized = fatSanitized(requested);
		// Directories, and files the provider didn't split an extension off, get the number at the end.
		if (isNumbered(actual, sanitized, "")) {
			return true;
		}
		final int dot = sanitized.lastIndexOf('.');
		return dot > 0 && isNumbered(actual, sanitized.substring(0, dot), sanitized.substring(dot));
	}

	// Whether name is "base (N)ext".
	private static boolean isNumbered(String name, String base, String ext) {
		final int digitsStart = base.length() + 2;
		final int digitsEnd = name.length() - ext.length() - 1;
		if (digitsEnd <= digitsStart || !name.startsWith(base + " (") || !name.endsWith(")" + ext)) {
			return false;
		}
		for (int i = digitsStart; i < digitsEnd; i++) {
			final char ch = name.charAt(i);
			if (ch < '0' || ch > '9') {
				return false;
			}
		}
		return true;
	}

	private static String fatSanitized(String name) {
		final StringBuilder sb = new StringBuilder(name.length());
		for (int i = 0; i < name.length(); i++) {
			final char ch = name.charAt(i);
			sb.append((ch <= 0x1f || ch == 0x7f || "\"*/:<>?\\|".indexOf(ch) >= 0) ? '_' : ch);
		}
		return sb.toString();
	}

	private static void logIfNameChanged(String what, String requested, String actual) {
		if (actual != null && !actual.equals(requested)) {
			Log.w(TAG, what + ": the provider named '" + requested + "' '" + actual + "'");
		}
	}

	// Without a client of our own, every query and DocumentsContract call acquires the storage provider and
	// releases it again, and when our reference count drops to zero, each of those is another binder call,
	// to the system. Holding one keeps the count up. Unstable, so the provider dying doesn't take our process
	// with it: queries that then fail drop it, and the next one acquires a new one.
	// One per authority, and only dropped when its provider dies, so a client is never closed while another
	// thread is still using it. There are rarely more than one or two.
	private static final Object clientLock = new Object();
	private static final HashMap<String, ContentProviderClient> clients = new HashMap<>();

	private static ContentProviderClient getClient(Context context, Uri uri) {
		final String authority = uri.getAuthority();
		if (authority == null) {
			return null;
		}
		synchronized (clientLock) {
			ContentProviderClient client = clients.get(authority);
			if (client == null) {
				// The application's resolver, so the client doesn't keep the activity alive.
				client = context.getApplicationContext().getContentResolver().acquireUnstableContentProviderClient(authority);
				if (client != null) {
					clients.put(authority, client);
				}
			}
			return client;
		}
	}

	// Drops the client if it's still the one cached for its authority (another thread may have replaced it already).
	private static void dropClient(String authority, ContentProviderClient client) {
		synchronized (clientLock) {
			if (clients.get(authority) != client) {
				return;
			}
			clients.remove(authority);
		}
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
			client.close();
		} else {
			client.release();
		}
	}

	private static Cursor query(Context context, Uri uri, String[] projection) throws Exception {
		final ContentProviderClient c = getClient(context, uri);
		if (c != null) {
			try {
				return c.query(uri, projection, null, null, null);
			} catch (android.os.DeadObjectException e) {
				Log.w(TAG, "Storage provider died, acquiring it again");
				dropClient(uri.getAuthority(), c);
			}
		}
		return context.getContentResolver().query(uri, projection, null, null, null);
	}

	// Like query. The resolver's own open is slower: it takes a stable reference to the provider for each
	// call, and drops it after.
	private static ParcelFileDescriptor openFile(Context context, Uri uri, String mode) throws Exception {
		final ContentProviderClient c = getClient(context, uri);
		if (c != null) {
			try {
				return c.openFile(uri, mode);
			} catch (android.os.DeadObjectException e) {
				Log.w(TAG, "Storage provider died, acquiring it again");
				dropClient(uri.getAuthority(), c);
			}
		}
		return context.getContentResolver().openFileDescriptor(uri, mode);
	}

	@Keep
	@SuppressWarnings("unused")
	public static int openContentUri(Activity activity, String uriString, String mode) {
		try {
			Uri uri = Uri.parse(uriString);
			try (ParcelFileDescriptor filePfd = openFile(activity, uri, mode)) {
				if (filePfd == null) {
					// I'd expect an exception to happen before we get here, so this is probably
					// never reached.
					Log.e(TAG, "Failed to get file descriptor for " + uriString);
					return STORAGE_ERROR_UNKNOWN;
				}
				// Review note: We intentionally "leak" the fd here, the caller takes ownership and closes
				// it when done with it.
				return filePfd.detachFd();
			}
		} catch (java.io.FileNotFoundException | IllegalArgumentException e) {
			// The IllegalArgumentException is long and ugly and really just means file not found too.
			// We don't log anything (the caller can log).
			return STORAGE_ERROR_NOT_FOUND;
		} catch (Exception e) {
			// Don't know when this might happen. Let's log. Still, the result is just a
			// failure that the caller may additionally log.
			Log.e(TAG, "Unexpected openContentUri exception: " + e);
			return errorFromException(e);
		}
	}

	private static String cursorToString(Cursor c, String prefixLowercase) {
		final int flags = c.getInt(2);
		// Filter out any virtual or partial nonsense.
		// There's a bunch of potentially-interesting flags here btw,
		// to figure out how to set access flags better, etc.
		// Like FLAG_SUPPORTS_WRITE etc.
		if ((flags & (DocumentsContract.Document.FLAG_PARTIAL | DocumentsContract.Document.FLAG_VIRTUAL_DOCUMENT)) != 0) {
			return null;
		}
		final String documentName = c.getString(0);
		if (documentName == null) {
			return null;
		}
		if (prefixLowercase != null && !prefixLowercase.isEmpty()) {
			// Filter by prefix, case insensitive.
			if (!documentName.regionMatches(true, 0, prefixLowercase, 0, prefixLowercase.length())) {
				return null;
			}
		}

		final String mimeType = c.getString(3);
		final boolean isDirectory = DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType);
		final long size = isDirectory ? 0 : c.getLong(1);
		final long lastModified = c.getLong(4);

		String str = "F|";
		if (isDirectory) {
			str = "D|";
		}
		return str + size + "|" + documentName + "|" + lastModified;
	}

	private static long directorySizeRecursion(Activity activity, Uri uri) {
		Cursor c = null;
		try {
			// Log.i(TAG, "recursing into " + uri.toString());
			final String[] columns = new String[]{
				DocumentsContract.Document.COLUMN_DOCUMENT_ID,
				DocumentsContract.Document.COLUMN_SIZE,
				DocumentsContract.Document.COLUMN_MIME_TYPE,  // check for MIME_TYPE_DIR
			};
			final String documentId = DocumentsContract.getDocumentId(uri);
			final Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, documentId);
			c = query(activity, childrenUri, columns);
			long sizeSum = 0;

			// Buffer the URIs so we only have one cursor active at once. I don't trust the storage framework
			// to handle more than one...
			ArrayList<Uri> childDirs = new ArrayList<>();

			if (c != null) {
				while (c.moveToNext()) {
					final String mimeType = c.getString(2);
					final boolean isDirectory = DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType);
					if (isDirectory) {
						final String childDocumentId = c.getString(0);
						final Uri childUri = DocumentsContract.buildDocumentUriUsingTree(uri, childDocumentId);
						childDirs.add(childUri);
					} else {
						final long fileSize = c.getLong(1);
						sizeSum += fileSize;
					}
				}
				c.close();
				c = null;
			}

			for (Uri childUri : childDirs) {
				long dirSize = directorySizeRecursion(activity, childUri);
				if (dirSize >= 0) {
					sizeSum += dirSize;
				} else {
					return dirSize;
				}
			}
			return sizeSum;
		} catch (Exception e) {
			return -1;
		} finally {
			if (c != null) {
				c.close();
			}
		}
	}

	@Keep
	@SuppressWarnings("unused")
	public static long computeRecursiveDirectorySize(Activity activity, String uriString) {
		try {
			Uri uri = Uri.parse(uriString);
			return directorySizeRecursion(activity, uri);
		}
		catch (Exception e) {
			Log.e(TAG, "computeRecursiveSize exception: " + e);
			return -1;
		}
	}

	// TODO: Maybe add a cheaper version that doesn't extract all the file information?
	// TODO: Replace with a proper query:
	// * https://stackoverflow.com/q
	// uestions/42186820/documentfile-is-very-slow
	@Keep
	@SuppressWarnings("unused")
	public static String[] listContentUriDir(Activity activity, String uriString, String prefix) {
		try {
			Uri uri = Uri.parse(uriString);
			final Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
				uri, DocumentsContract.getDocumentId(uri));
			final ArrayList<String> listing = new ArrayList<>();

			String[] projection = {
				DocumentsContract.Document.COLUMN_DISPLAY_NAME,   // index 0
				DocumentsContract.Document.COLUMN_SIZE,           // index 1
				DocumentsContract.Document.COLUMN_FLAGS,          // index 2
				DocumentsContract.Document.COLUMN_MIME_TYPE,      // index 3
				DocumentsContract.Document.COLUMN_LAST_MODIFIED   // index 4
			};

			if (prefix != null) {
				prefix = prefix.toLowerCase(Locale.ROOT);
			}

			try (Cursor c = query(activity, childrenUri, projection)) {
				if (c == null) {
					return new String[]{"X"};
				}
				while (c.moveToNext()) {
					String str = cursorToString(c, prefix);
					if (str != null) {
						listing.add(str);
					}
				}
			}

			return listing.toArray(new String[0]);
		} catch (IllegalArgumentException e) {
			return new String[]{"X"};
		} catch (Exception e) {
			Log.e(TAG, "listContentUriDir exception: " + e);
			return new String[]{"X"};
		}
	}

	// Given a taken name, the provider makes "name (1)" instead of failing. Nothing may leave such a duplicate of
	// the user's files, so that's undone here and reported as ALREADY_EXISTS.
	private static int createDocument(Activity activity, String parentUriString, String mimeType, String name) {
		try {
			Uri parentUri = Uri.parse(parentUriString);
			if (!DocumentsContract.isDocumentUri(activity, parentUri)) {
				parentUri = DocumentsContract.buildDocumentUriUsingTree(parentUri, DocumentsContract.getTreeDocumentId(parentUri));
			}
			final ContentResolver resolver = activity.getContentResolver();
			final Uri created = DocumentsContract.createDocument(resolver, parentUri, mimeType, name);
			if (created == null) {
				return STORAGE_ERROR_UNKNOWN;
			}
			// Null if the provider wouldn't say, which isn't a conflict.
			final String createdName = documentName(activity, created);
			if (isRenamedCopy(name, createdName)) {
				Log.w(TAG, "createDocument: '" + name + "' is taken, removing the renamed copy " + created);
				DocumentsContract.deleteDocument(resolver, created);
				return STORAGE_ERROR_ALREADY_EXISTS;
			}
			logIfNameChanged("createDocument", name, createdName);
			return STORAGE_ERROR_SUCCESS;
		} catch (Exception e) {
			Log.e(TAG, "createDocument exception: " + e);
			return errorFromException(e);
		}
	}

	@Keep
	@SuppressWarnings("unused")
	public static int contentUriCreateDirectory(Activity activity, String rootTreeUri, String dirName) {
		return createDocument(activity, rootTreeUri, DocumentsContract.Document.MIME_TYPE_DIR, dirName);
	}

	// Fails with ALREADY_EXISTS if the name is taken.
	@Keep
	@SuppressWarnings("unused")
	public static int contentUriCreateFile(Activity activity, String rootTreeUri, String fileName) {
		return createDocument(activity, rootTreeUri, "application/octet-stream", fileName);
	}

	@Keep
	public static int contentUriRemoveFile(Activity activity, String fileName) {
		try {
			Uri uri = Uri.parse(fileName);
			DocumentFile documentFile = DocumentFile.fromSingleUri(activity, uri);
			if (documentFile != null) {
				return documentFile.delete() ? STORAGE_ERROR_SUCCESS : STORAGE_ERROR_UNKNOWN;
			} else {
				// This can return null on old Android versions (that we no longer supports).
				return STORAGE_ERROR_UNKNOWN;
			}
		} catch (Exception e) {
			Log.e(TAG, "contentUriRemoveFile exception: " + e);
			return errorFromException(e);
		}
	}

	// NOTE: The destination is the parent directory! This means that contentUriCopyFile
	// cannot rename things as part of the operation.
	@RequiresApi(Build.VERSION_CODES.N)
	@Keep
	@SuppressWarnings("unused")
	public static int contentUriCopyFile(Activity activity, String srcFileUri, String dstParentDirUri) {
		// The folder browser is offered from API 21. Fail cleanly so the caller falls back.
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
			return STORAGE_ERROR_UNKNOWN;
		}
		try {
			Uri srcUri = Uri.parse(srcFileUri);
			Uri dstParentUri = Uri.parse(dstParentDirUri);
			final ContentResolver resolver = activity.getContentResolver();
			final Uri copy = DocumentsContract.copyDocument(resolver, srcUri, dstParentUri);
			if (copy == null) {
				return STORAGE_ERROR_UNKNOWN;
			}
			// A taken name gets a renamed copy, see createDocument.
			final String name = documentName(activity, srcUri);
			final String copyName = documentName(activity, copy);
			if (name != null && isRenamedCopy(name, copyName)) {
				Log.w(TAG, "contentUriCopyFile: '" + name + "' is taken, removing the renamed copy " + copy);
				DocumentsContract.deleteDocument(resolver, copy);
				return STORAGE_ERROR_ALREADY_EXISTS;
			}
			return STORAGE_ERROR_SUCCESS;
		} catch (Exception e) {
			Log.e(TAG, "contentUriCopyFile exception: " + e);
			return errorFromException(e);
		}
	}

	// NOTE: The destination is the parent directory! This means that contentUriCopyFile
	// cannot rename things as part of the operation.
	@RequiresApi(Build.VERSION_CODES.N_MR1)
	@Keep
	@SuppressWarnings("unused")
	public static int contentUriMoveFile(Activity activity, String srcFileUri, String srcParentDirUri, String dstParentDirUri) {
		// The folder browser is offered from API 21. Fail cleanly so the caller falls back.
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N_MR1) {
			return STORAGE_ERROR_UNKNOWN;
		}
		try {
			Uri srcUri = Uri.parse(srcFileUri);
			Uri srcParentUri = Uri.parse(srcParentDirUri);
			Uri dstParentUri = Uri.parse(dstParentDirUri);
			Log.i(TAG, "DocumentsContract.moveDocument");
			int result = DocumentsContract.moveDocument(activity.getContentResolver(), srcUri, srcParentUri, dstParentUri) != null ? STORAGE_ERROR_SUCCESS : STORAGE_ERROR_UNKNOWN;
			Log.i(TAG, "DocumentsContract.moveDocument done");
			return result;
		} catch (Exception e) {
			Log.e(TAG, "contentUriMoveFile exception: " + e);
			return errorFromException(e);
		}
	}

	@Keep
	@SuppressWarnings("unused")
	public static int contentUriRenameFileTo(Activity activity, String fileUri, String newName) {
		try {
			Uri uri = Uri.parse(fileUri);
			// Due to a design flaw, we can't use DocumentFile.renameTo().
			// Instead we use the DocumentsContract API directly.
			// See https://stackoverflow.com/questions/37168200/android-5-0-new-sd-card-access-api-documentfile-renameto-unsupportedoperation.
			final ContentResolver resolver = activity.getContentResolver();
			final String oldName = documentName(activity, uri);
			final Uri newUri = DocumentsContract.renameDocument(resolver, uri, newName);
			// A taken name gets "name (1)" instead, see createDocument. Put the old name back.
			final String renamedName = newUri != null ? documentName(activity, newUri) : null;
			if (oldName != null && isRenamedCopy(newName, renamedName)) {
				Log.w(TAG, "contentUriRenameFileTo: '" + newName + "' is taken, renaming back to '" + oldName + "'");
				DocumentsContract.renameDocument(resolver, newUri, oldName);
				return STORAGE_ERROR_ALREADY_EXISTS;
			}
			logIfNameChanged("contentUriRenameFileTo", newName, renamedName);
			return STORAGE_ERROR_SUCCESS;
		} catch (Exception e) {
			Log.e(TAG, "contentUriRenameFile exception: " + e);
			return errorFromException(e);
		}
	}

	private static void closeQuietly(AutoCloseable closeable) {
		if (closeable != null) {
			try {
				closeable.close();
			} catch (RuntimeException rethrown) {
				throw rethrown;
			} catch (Exception ignored) {
			}
		}
	}

	// Probably slightly faster than contentUriGetFileInfo.
	// Smaller difference now than before I changed that one to a query...
	@Keep
	@SuppressWarnings("unused")
	public static boolean contentUriFileExists(Activity activity, String fileUri) {
		Cursor c = null;
		try {
			Uri uri = Uri.parse(fileUri);
			c = query(activity, uri, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID });
			if (c != null) {
				return c.getCount() > 0;
			} else {
				return false;
			}
		} catch (Exception e) {
			// Log.w(TAG, "Failed query: " + e);
			return false;
		} finally {
			closeQuietly(c);
		}
	}

	@Keep
	@SuppressWarnings("unused")
	public static String contentUriGetFileInfo(Activity activity, String fileName) {
		String[] projection = {
			DocumentsContract.Document.COLUMN_DISPLAY_NAME,   // index 0
			DocumentsContract.Document.COLUMN_SIZE,           // index 1
			DocumentsContract.Document.COLUMN_FLAGS,          // index 2
			DocumentsContract.Document.COLUMN_MIME_TYPE,      // index 3
			DocumentsContract.Document.COLUMN_LAST_MODIFIED   // index 4
		};

		try (Cursor c = query(activity, Uri.parse(fileName), projection)) {
			if (c != null && c.moveToFirst()) {
				return cursorToString(c, null);
			} else {
				return null;
			}
		} catch (Exception e) {
			Log.e(TAG, "contentUriGetFileInfo exception: " + e);
			return null;
		}
	}

	// The example in Android documentation uses this.getFilesDir for path.
	// There's also a way to beg the OS for more space, which might clear caches, but
	// let's just not bother with that for now.
	// NOTE: This is really super slow!
	@RequiresApi(Build.VERSION_CODES.M)
	@Keep
	@SuppressWarnings("unused")
	public static long contentUriGetFreeStorageSpaceSlow(Activity activity, Uri uri) {
		try (ParcelFileDescriptor pfd = activity.getContentResolver().openFileDescriptor(uri, "r")) {
			if (pfd == null) {
				Log.w(TAG, "Failed to get free storage space from URI: " + uri);
				return -1;
			}
			StructStatVfs stats = Os.fstatvfs(pfd.getFileDescriptor());
			return stats.f_bavail * stats.f_bsize;
		} catch (Exception e) {
			// FileNotFoundException | ErrnoException e
			// Log.getStackTraceString(e)
			Log.e(TAG, "contentUriGetFreeStorageSpace exception: " + e);
			return -1;
		}
	}

	// The free space of the storage volume a document is on, looked up from the volume in its document ID
	// ("primary:PSP", "75D7-DC5F:PSP"). The fstatvfs of a descriptor opened through the provider can't be
	// trusted for that: for a folder on an SD card, some devices report the internal storage. -1 if the volume
	// isn't found.
	@RequiresApi(Build.VERSION_CODES.R)
	private static long getFreeSpaceOfVolume(Context context, Uri uri) {
		try {
			if (!"com.android.externalstorage.documents".equals(uri.getAuthority())) {
				return -1;
			}
			final String documentId = DocumentsContract.isDocumentUri(context, uri) ? DocumentsContract.getDocumentId(uri) : DocumentsContract.getTreeDocumentId(uri);
			final int colon = documentId.indexOf(':');
			if (colon <= 0) {
				return -1;
			}
			final String volumeId = documentId.substring(0, colon);
			final StorageManager storageManager = context.getSystemService(StorageManager.class);
			for (StorageVolume volume : storageManager.getStorageVolumes()) {
				final boolean match = "primary".equals(volumeId) ? volume.isPrimary() : volumeId.equalsIgnoreCase(volume.getUuid());
				final File directory = match ? volume.getDirectory() : null;
				if (directory != null) {
					return new StatFs(directory.getPath()).getAvailableBytes();
				}
			}
		} catch (Exception e) {
			Log.w(TAG, "getFreeSpaceOfVolume: " + e);
		}
		return -1;
	}

	@Keep
	@SuppressWarnings("unused")
	public static long contentUriGetFreeStorageSpace(Activity activity, String str) {
		Uri uri = Uri.parse(str);
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			long space = getFreeSpaceOfVolume(activity, uri);
			if (space >= 0) {
				return space;
			}
		}
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			return contentUriGetFreeStorageSpaceSlow(activity, uri);
		}

		// Too early Android version
		return -1;
	}

	// Android only lets an app keep so many persisted URI grants (512, or 128 before Android 11). When it
	// goes over, the system drops the oldest ones, whatever they are. We take one for every single file
	// picked (games loaded through the file picker, home screen shortcuts, sound effects) and never gave any
	// back, so sooner or later the one dropped would be the folder the user set up as the memory stick.
	//
	// Two things at startup keep that from happening:
	// * Re-take the grants for the folders we use (the memory stick, the Games tab, pinned folders, the Remote
	//   ISO shared folder). That makes them the newest, so they're the last the system would drop.
	// * Past half the limit, release the oldest single-file grants that nothing points at anymore. Folder
	//   grants are never released. Home screen shortcuts can't be enumerated, so ppsspp.ini remembers the
	//   ones most recently created or launched (Config::MAX_SHORTCUT_URIS), and those are spared too.
	private static final String GRANT_TAG = "PPSSPPUriGrants";

	private static boolean isTreeUri(Uri uri) {
		// Same check as DocumentsContract.isTreeUri, which is API 24+.
		List<String> segments = uri.getPathSegments();
		return segments.size() >= 2 && "tree".equals(segments.get(0));
	}

	// True if something points at the grant's URI, or at something inside it (if it's a folder).
	static boolean isReferenced(Uri grantUri, Set<String> referencedUris) {
		final String uri = grantUri.toString();
		if (referencedUris.contains(uri)) {
			return true;
		}
		final String prefix = uri + "/";
		for (String referenced : referencedUris) {
			if (referenced.startsWith(prefix)) {
				return true;
			}
		}
		return false;
	}

	private static int grantFlags(UriPermission grant) {
		int flags = 0;
		if (grant.isReadPermission()) {
			flags |= Intent.FLAG_GRANT_READ_URI_PERMISSION;
		}
		if (grant.isWritePermission()) {
			flags |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
		}
		return flags;
	}

	// Returns the grants to release to get down to maxGrants, oldest first. Doesn't release anything.
	static ArrayList<UriPermission> chooseGrantsToRelease(List<UriPermission> grants, int maxGrants, Set<String> referencedUris) {
		ArrayList<UriPermission> toRelease = new ArrayList<>();
		int excess = grants.size() - maxGrants;
		if (excess <= 0) {
			return toRelease;
		}
		ArrayList<UriPermission> oldestFirst = new ArrayList<>(grants);
		Collections.sort(oldestFirst, (a, b) -> Long.compare(a.getPersistedTime(), b.getPersistedTime()));
		for (UriPermission grant : oldestFirst) {
			if (toRelease.size() >= excess) {
				break;
			}
			if (isTreeUri(grant.getUri()) || isReferenced(grant.getUri(), referencedUris)) {
				continue;
			}
			toRelease.add(grant);
		}
		return toRelease;
	}

	// Call once at startup, after the native side has loaded the config and been handed the queued shortcut URIs.
	public static void manageUriGrants(Context context) {
		try {
			final ContentResolver resolver = context.getContentResolver();
			final List<UriPermission> grants = resolver.getPersistedUriPermissions();
			final int systemLimit = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R ? 512 : 128;
			final int maxGrants = systemLimit / 2;
			final String[] referenced = NativeApp.getReferencedUris();
			if (referenced == null) {
				// We can't tell what's in use, so leave everything alone.
				Log.w(GRANT_TAG, "Don't have the user's config, not touching any of our " + grants.size() + " URI grants.");
				return;
			}
			final Set<String> referencedUris = new HashSet<>(Arrays.asList(referenced));

			int folders = 0;
			for (UriPermission grant : grants) {
				if (isTreeUri(grant.getUri())) {
					folders++;
				}
			}
			Log.i(GRANT_TAG, "Holding " + grants.size() + " persisted URI grants: " + folders + " folders, " + (grants.size() - folders)
				+ " files. The system drops the oldest above " + systemLimit + ", we start releasing old files above " + maxGrants + ".");

			// Release before re-taking: a take can make the system prune, which would leave our list out of date.
			if (grants.size() > maxGrants) {
				Log.i(GRANT_TAG, "The recent list, shortcuts and settings reference " + referencedUris.size() + " URIs.");
				ArrayList<UriPermission> toRelease = chooseGrantsToRelease(grants, maxGrants, referencedUris);
				int released = 0;
				for (UriPermission grant : toRelease) {
					Log.i(GRANT_TAG, "Releasing unused file grant: " + grant.getUri() + " taken " + new Date(grant.getPersistedTime()));
					try {
						resolver.releasePersistableUriPermission(grant.getUri(), grantFlags(grant));
						released++;
					} catch (Exception e) {
						Log.w(GRANT_TAG, "Couldn't release " + grant.getUri() + ": " + e);
					}
				}
				if (toRelease.isEmpty()) {
					Log.w(GRANT_TAG, "Over our limit, but everything is a folder or still in use. Nothing to release.");
				} else {
					Log.i(GRANT_TAG, "Released " + released + " file grants, " + (grants.size() - released) + " remain.");
				}
			}

			for (UriPermission grant : grants) {
				if (!isTreeUri(grant.getUri())) {
					continue;
				}
				if (!isReferenced(grant.getUri(), referencedUris)) {
					Log.i(GRANT_TAG, "Folder grant (kept, not in use): " + grant.getUri() + " taken " + new Date(grant.getPersistedTime()));
					continue;
				}
				// Taking a grant we already hold just moves its time up.
				try {
					resolver.takePersistableUriPermission(grant.getUri(), grantFlags(grant));
					Log.i(GRANT_TAG, "Folder grant (in use, re-taken): " + grant.getUri() + " taken " + new Date(grant.getPersistedTime()));
				} catch (Exception e) {
					Log.w(GRANT_TAG, "Couldn't re-take folder grant " + grant.getUri() + ": " + e);
				}
			}
		} catch (Exception e) {
			Log.e(GRANT_TAG, "Exception while managing URI grants: " + e);
		}
	}

	// ShortcutActivity can't write ppsspp.ini (the config isn't loaded there, and a running PpssppActivity would
	// overwrite it), so the URIs of shortcuts created or launched wait here until the next startup hands them over.
	private static final String SHORTCUT_PREFS = "PendingShortcutUris";
	private static final String SHORTCUT_PREFS_KEY = "uris";
	private static final int MAX_PENDING_SHORTCUT_URIS = 64;

	private static ArrayList<String> readPendingShortcutUris(SharedPreferences prefs) {
		ArrayList<String> uris = new ArrayList<>();
		for (String uri : prefs.getString(SHORTCUT_PREFS_KEY, "").split("\n")) {
			if (!uri.isEmpty()) {
				uris.add(uri);
			}
		}
		return uris;
	}

	public static void queueShortcutUri(Context context, String uri) {
		if (uri == null || !uri.startsWith("content://")) {
			return;
		}
		SharedPreferences prefs = context.getSharedPreferences(SHORTCUT_PREFS, Context.MODE_PRIVATE);
		ArrayList<String> uris = readPendingShortcutUris(prefs);
		uris.remove(uri);
		uris.add(uri);  // Newest last.
		while (uris.size() > MAX_PENDING_SHORTCUT_URIS) {
			uris.remove(0);
		}
		prefs.edit().putString(SHORTCUT_PREFS_KEY, TextUtils.join("\n", uris)).commit();
	}

	// Call once at startup, after the native side has loaded the config.
	public static void flushShortcutUris(Context context) {
		SharedPreferences prefs = context.getSharedPreferences(SHORTCUT_PREFS, Context.MODE_PRIVATE);
		ArrayList<String> uris = readPendingShortcutUris(prefs);
		if (uris.isEmpty()) {
			return;
		}
		if (!NativeApp.addShortcutUris(uris.toArray(new String[0]))) {
			Log.w(GRANT_TAG, "Couldn't save " + uris.size() + " shortcut URIs to the config, keeping them for next time.");
			return;
		}
		Log.i(GRANT_TAG, "Saved " + uris.size() + " shortcut URIs to the config.");
		prefs.edit().remove(SHORTCUT_PREFS_KEY).commit();
	}

	@Keep
	@SuppressWarnings("unused")
	public static boolean isExternalStoragePreservedLegacy() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
			// In 29 and later, we can check whether we got preserved storage legacy.
			return Environment.isExternalStorageLegacy();
		} else {
			// In 28 and earlier, we won't call this - we'll still request an exception.
			return false;
		}
	}
}
