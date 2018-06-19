package org.ppsspp.ppsspp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.os.Environment;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/** Simple dialog to pick file. */
public class SimpleFileChooser {
	public interface FileSelectedListener {
		void onFileSelected(File file);
	}

	private FileSelectedListener mFileListener;

	private final Activity mActivity;
	private static final String PARENT_DIR = "..";
	private String[] mFileList;
	private File mCurrentPath;

	public SimpleFileChooser(Activity activity, File path, FileSelectedListener listener) {
		this.mActivity = activity;
		this.mFileListener = listener;
		if (!path.exists())
			path = Environment.getExternalStorageDirectory();
		rebuildFileList(path);
	}

	public void showDialog() {
		AlertDialog.Builder builder = new AlertDialog.Builder(mActivity);
		builder.setTitle(mCurrentPath.getPath());
		// populate dialog with list of files and directories.
		builder.setItems(mFileList, onDialogItemClickedListener);
		builder.show();
	}

	// Create list of files and directories.
	private void rebuildFileList(File path) {
		this.mCurrentPath = path;
		List<String> r = new ArrayList<String>();

		if (path.getParentFile() != null)
			r.add(PARENT_DIR);

		File[] fileList = path.listFiles();
		if (fileList != null) {
			Arrays.sort(fileList, fileArrayComparator);
			for (File file : fileList) {
				r.add(file.getName());
			}
		}
		mFileList = (String[]) r.toArray(new String[0]);
	}

	// Get selected file, dir, or parent dir.
	private File getSelectedFile(String selectedFileName) {
		if (selectedFileName.equals(PARENT_DIR))
			return mCurrentPath.getParentFile();
		else
			return new File(mCurrentPath, selectedFileName);
	}

	// Comparator for Arrays.sort(). Separate folders from files, order
	// alphabetically, ignore case.
	private Comparator<File> fileArrayComparator = new Comparator<File>() {
		public int compare(File file1, File file2) {
			if (file1 == null || file2 == null) // if either null, assume equal
				return 0;
			// put folder first before file
			else if (file1.isDirectory() && (!file2.isDirectory()))
				return -1;
			else if (file2.isDirectory() && (!file1.isDirectory()))
				return 1;
			else
				// when both are folders or both are files, sort by name
				return file1.getName().toUpperCase().compareTo(file2.getName().toUpperCase());
		}
	};

	// Event when user click item on dialog
	private DialogInterface.OnClickListener onDialogItemClickedListener = new DialogInterface.OnClickListener() {
		public void onClick(DialogInterface dialog, int which) {
			String selectedFileName = mFileList[which];
			File selectedFile = getSelectedFile(selectedFileName);

			// always remove previous dlg first
			dialog.cancel();
			dialog.dismiss();

			if (selectedFile.isDirectory()) {
				rebuildFileList(selectedFile);
				showDialog(); // create new dlg
			} else {
				if (mFileListener != null)
					mFileListener.onFileSelected(selectedFile);
			}
		}
	};
}
