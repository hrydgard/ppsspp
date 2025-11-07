package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.storage.StorageManager;
import android.provider.DocumentsContract;
import android.system.Os;
import android.system.StructStatVfs;
import android.util.Log;

import androidx.annotation.Keep;
import androidx.annotation.RequiresApi;
import androidx.documentfile.provider.DocumentFile;

import java.io.File;
import java.util.ArrayList;
import java.util.UUID;

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
				return filePfd.detachFd();  // Take ownership of the fd.
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

	private static final String[] columns = new String[] {
		DocumentsContract.Document.COLUMN_DISPLAY_NAME,
		DocumentsContract.Document.COLUMN_SIZE,
		DocumentsContract.Document.COLUMN_FLAGS,
		DocumentsContract.Document.COLUMN_MIME_TYPE,  // check for MIME_TYPE_DIR
		DocumentsContract.Document.COLUMN_LAST_MODIFIED
	};

	private static String cursorToString(Cursor c) {
		final int flags = c.getInt(2);
		// Filter out any virtual or partial nonsense.
		// There's a bunch of potentially-interesting flags here btw,
		// to figure out how to set access flags better, etc.
		// Like FLAG_SUPPORTS_WRITE etc.
		if ((flags & (DocumentsContract.Document.FLAG_PARTIAL | DocumentsContract.Document.FLAG_VIRTUAL_DOCUMENT)) != 0) {
			return null;
		}
		final String mimeType = c.getString(3);
		final boolean isDirectory = mimeType.equals(DocumentsContract.Document.MIME_TYPE_DIR);
		final String documentName = c.getString(0);
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
			}
			c = null;

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
	public static String[] listContentUriDir(Activity activity, String uriString) {
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

			try (Cursor c = resolver.query(childrenUri, projection, null, null, null)) {
				if (c == null) {
					return new String[]{"X"};
				}
				while (c.moveToNext()) {
					String str = cursorToString(c);
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
				Log.e(TAG, "contentUriCreateFile: fromTreeUrisv returned null");
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
				return cursorToString(c);
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

	@RequiresApi(Build.VERSION_CODES.O)
	@Keep
	@SuppressWarnings("unused")
	public static long filePathGetFreeStorageSpace(Activity activity, String filePath) {
		try {
			StorageManager storageManager = activity.getApplicationContext().getSystemService(StorageManager.class);
			File file = new File(filePath);
			UUID volumeUUID = storageManager.getUuidForPath(file);
			return storageManager.getAllocatableBytes(volumeUUID);
		} catch (Exception e) {
			Log.e(TAG, "filePathGetFreeStorageSpace exception: " + e);
			return -1;
		}
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
