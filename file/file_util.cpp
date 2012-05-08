#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#include <string>
#include <stdio.h>
#include "base/logging.h"
#include "base/basictypes.h"
#include "file/file_util.h"
#include <sys/stat.h>

bool writeStringToFile(bool text_file, const std::string &str, const char *filename)
{
  FILE *f = fopen(filename, text_file ? "w" : "wb");
  if (!f)
    return false;
  size_t len = str.size();
  if (len != fwrite(str.data(), 1, str.size(), f))
  {
    fclose(f);
    return false;
  }
  fclose(f);
  return true;
}

uint64_t GetSize(FILE *f)
{
  // can't use off_t here because it can be 32-bit
  uint64_t pos = ftell(f);
  if (fseek(f, 0, SEEK_END) != 0) {
    return 0;
  }
  uint64_t size = ftell(f);
  // Reset the seek position to where it was when we started.
  if ((size != pos) && (fseek(f, pos, SEEK_SET) != 0)) {
    // Should error here
    return 0;
  }
  return size;
}

bool ReadFileToString(bool text_file, const char *filename, std::string &str)
{
  FILE *f = fopen(filename, text_file ? "r" : "rb");
  if (!f)
    return false;
  size_t len = (size_t)GetSize(f);
  char *buf = new char[len + 1];
  buf[fread(buf, 1, len, f)] = 0;
  str = std::string(buf, len);
  fclose(f);
  delete [] buf;
  return true;
}

#define DIR_SEP "/"

#ifndef METRO

size_t getFilesInDir(const char *directory, std::vector<std::string> *files)
{
  size_t foundEntries = 0;
#ifdef _WIN32
  // Find the first file in the directory.
  WIN32_FIND_DATA ffd;
#ifdef UNICODE
  HANDLE hFind = FindFirstFile((std::wstring(directory) + "\\*").c_str(), &ffd);
#else
  HANDLE hFind = FindFirstFile((std::string(directory) + "\\*").c_str(), &ffd);
#endif
  if (hFind == INVALID_HANDLE_VALUE)
  {
    FindClose(hFind);
    return foundEntries;
  }
  // windows loop
  do
  {
    const std::string virtualName(ffd.cFileName);
#else
    struct dirent dirent, *result = NULL;

    DIR *dirp = opendir(directory);
    if (!dirp)
      return 0;

    // non windows loop
    while (!readdir_r(dirp, &dirent, &result) && result)
    {
      const std::string virtualName(result->d_name);
#endif
    // check for "." and ".."
    if (((virtualName[0] == '.') && (virtualName[1] == '\0')) ||
      ((virtualName[0] == '.') && (virtualName[1] == '.') && 
      (virtualName[2] == '\0')))
      continue;

    files->push_back(std::string(directory) + virtualName);
#ifdef _WIN32 
  } while (FindNextFile(hFind, &ffd) != 0);
  FindClose(hFind);
#else
  }
  closedir(dirp);
#endif
  return foundEntries;
}

void deleteFile(const char *file)
{
#ifdef _WIN32
  if (!DeleteFile(file)) {
    ELOG("Error deleting %s: %i", file, GetLastError());
  }
#else
  int err = unlink(file);
  if (err) {
    ELOG("Error unlinking %s: %i", file, err);
  }
#endif
}
#endif

std::string getDir(const std::string &path)
{
  int n = path.size() - 1;
  while (n >= 0 && path[n] != '\\' && path[n] != '/')
    n--;
  std::string cutpath = path.substr(0, n);
  for (size_t i = 0; i < cutpath.size(); i++)
  {  
    if (cutpath[i] == '\\') cutpath[i] = '/';
  }
  return cutpath;
}