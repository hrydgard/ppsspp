package org.ppsspp.ppsspp;

import android.app.Activity;
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

	@Keep
	@SuppressWarnings("unused")
	public static int openContentUri(Activity activity, String uriString, String mode) {
		try {
			Uri uri = Uri.parse(uriString);
			try (ParcelFileDescriptor filePfd = activity.getContentResolver().openFileDescriptor(uri, mode)) {
				if (filePfd == null) {
					// I'd expect an exception to happen before we get here, so this is probably
					// never reached.
					Log.e(TAG, "Failed to get file descriptor for " + uriString);
					return -1;
				}
				// Review note: We intentionally "leak" the fd here, the caller takes ownership and closes
				// it when done with it.
				return filePfd.detachFd();
			}
		} catch (java.lang.IllegalArgumentException e) {
			// This exception is long and ugly and really just means file not found.
			// We don't log anything (the caller can log).
			return -1;
		} catch (Exception e) {
			// Don't know when this might happen. Let's log. Still, the result is just a
			// failure that the caller may additionally log.
			Log.e(TAG, "Unexpected openContentUri exception: " + e);
			return -1;
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
			final ContentResolver resolver = activity.getContentResolver();
			final String documentId = DocumentsContract.getDocumentId(uri);
			final Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, documentId);
			c = resolver.query(childrenUri, columns, null, null, null);
			long sizeSum = 0;

			// Buffer the URIs so we only have one cursor active at once. I don't trust the storage framework
			// to handle more than one...
			ArrayList<Uri> childDirs = new ArrayList<>();

			if (c != null) {
				while (c.moveToNext()) {
					final String mimeType = c.getString(2);
					final boolean isDirectory = mimeType.equals(DocumentsContract.Document.MIME_TYPE_DIR);
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
			final ContentResolver resolver = activity.getContentResolver();
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

			try (Cursor c = resolver.query(childrenUri, projection, null, null, null)) {
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

	@Keep
	@SuppressWarnings("unused")
	public static int contentUriCreateDirectory(Activity activity, String rootTreeUri, String dirName) {
		try {
			Uri uri = Uri.parse(rootTreeUri);
			DocumentFile documentFile = DocumentFile.fromTreeUri(activity, uri);
			if (documentFile != null) {
				DocumentFile createdDir = documentFile.createDirectory(dirName);
				return createdDir != null ? STORAGE_ERROR_SUCCESS : STORAGE_ERROR_UNKNOWN;
			} else {
				Log.e(TAG, "contentUriCreateDirectory: fromTreeUri returned null");
				return STORAGE_ERROR_UNKNOWN;
			}
		} catch (Exception e) {
			Log.e(TAG, "contentUriCreateDirectory exception: " + e);
			return STORAGE_ERROR_UNKNOWN;
		}
	}

	@Keep
	@SuppressWarnings("unused")
	// NOTE: If the file already exists, this will NOT return an error, instead the OS will create a new
	// file appending an incrementing number, for example (1), to the filename.
	public static int contentUriCreateFile(Activity activity, String rootTreeUri, String fileName) {
		try {
			Uri uri = Uri.parse(rootTreeUri);
			DocumentFile documentFile = DocumentFile.fromTreeUri(activity, uri);
			if (documentFile != null) {
				// TODO: Check the file extension and choose MIME type appropriately.
				// Or actually, let's not bother.
				DocumentFile createdFile = documentFile.createFile("application/octet-stream", fileName);
				return createdFile != null ? STORAGE_ERROR_SUCCESS : STORAGE_ERROR_UNKNOWN;
			} else {
				Log.e(TAG, "contentUriCreateFile: fromTreeUri returned null");
				return STORAGE_ERROR_UNKNOWN;
			}
		} catch (Exception e) {
			Log.e(TAG, "contentUriCreateFile exception: " + e);
			return STORAGE_ERROR_UNKNOWN;
		}
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
			return STORAGE_ERROR_UNKNOWN;
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
			return DocumentsContract.copyDocument(activity.getContentResolver(), srcUri, dstParentUri) != null ? STORAGE_ERROR_SUCCESS : STORAGE_ERROR_UNKNOWN;
		} catch (Exception e) {
			Log.e(TAG, "contentUriCopyFile exception: " + e);
			return STORAGE_ERROR_UNKNOWN;
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
			return STORAGE_ERROR_UNKNOWN;
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
			Uri newUri = DocumentsContract.renameDocument(activity.getContentResolver(), uri, newName);
			// NOTE: we don't use the returned newUri for anything right now.
			return STORAGE_ERROR_SUCCESS;
		} catch (Exception e) {
			// TODO: More detailed exception processing.
			Log.e(TAG, "contentUriRenameFile exception: " + e);
			return STORAGE_ERROR_UNKNOWN;
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
			c = activity.getContentResolver().query(uri, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID }, null, null, null);
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

		try (Cursor c = activity.getContentResolver().query(Uri.parse(fileName), projection, null, null, null)) {
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
		try {
			ParcelFileDescriptor pfd = activity.getContentResolver().openFileDescriptor(uri, "r");
			if (pfd == null) {
				Log.w(TAG, "Failed to get free storage space from URI: " + uri);
				return -1;
			}
			StructStatVfs stats = Os.fstatvfs(pfd.getFileDescriptor());
			long freeSpace = stats.f_bavail * stats.f_bsize;
			pfd.close();
			return freeSpace;
		} catch (Exception e) {
			// FileNotFoundException | ErrnoException e
			// Log.getStackTraceString(e)
			Log.e(TAG, "contentUriGetFreeStorageSpace exception: " + e);
			return -1;
		}
	}

	@Keep
	@SuppressWarnings("unused")
	public static long contentUriGetFreeStorageSpace(Activity activity, String str) {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			Uri uri = Uri.parse(str);
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
