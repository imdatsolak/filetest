/*
 * filetest - a program to test performance of a filesystem based on requirements
 * for a filesystem with billions of files with sizes ranging from 0B - 2MB
 *
 * Created: 2014-06-27 Imdat Solak (iso)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <sys/stat.h>

#define MAX(a,b) (a>b? (a):(b))
#define MIN(a,b) (a<b? (a):(b))
#define LDEBUG ldebug
#define NANO 1000000000L

/* * Nano-Time Stuff ****************************************** */

/* 
 * author: jbenet
 * os x, compile with: gcc -o testo test.c 
 * linux, compile with: gcc -o testo test.c -lrt
 */
 
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif
 
 
void current_utc_time(struct timespec *ts) {
 
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
   clock_serv_t cclock;
   mach_timespec_t mts;
   host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
   clock_get_time(cclock, &mts);
   mach_port_deallocate(mach_task_self(), cclock);
   ts->tv_sec = mts.tv_sec;
   ts->tv_nsec = mts.tv_nsec;
#else
   clock_gettime(CLOCK_REALTIME, ts);
#endif
 
}

/* End Nano-Time-stuff ************************************** */


double timeNano()
{
   struct timespec ts;

   current_utc_time(&ts);
   return ts.tv_sec * NANO + ts.tv_nsec;
}

typedef struct filenames {
   char fileName[36];
   int fileSize;
   double nsecs;
} filenamesT;

typedef struct filenamesL {
   char fileName[255];
   int fileSize;
   double nsecs;
} filenamesLT;

typedef struct fileSize {
   int decileMin;
   int decileMax;
   int decileNumFiles;
} fileSizeT;

double minReadBytesPns = 10000000.0;
double maxReadBytesPns = 0.0;
double avgReadBytesPns = 0.0;

double minRReadBytesPns = 100000000.0;
double maxRReadBytesPns = 0.0;
double avgRReadBytesPns = 0.0;

double minWriteBytesPns = 10000000.0;
double maxWriteBytesPns = 0.0;
double avgWriteBtyesPns = 0.0;

double minDeleteFilesPns = 10000000.0;
double maxDeleteFilesPns = 0.0;
double avgDeleteFilesPns = 0.0;

char   fsPath[1024];                           // Filesystem base path to start writing files
char   logFile[1024];                           // Logfile base name (forked) or full-name (unforked)
int      numFilesToCreate = 100000000;               // 100 million files is default
int      splitStatsEvery = 10000;                  // Split stats every tenthousand files
int      fsBlkSize = 4096;                        // Standard FileSystem Block Size (to generate fragmentation)
int      readFiles = 1;                           // Perform Reads?
int      randomReads = 1;                        // Perform Random Reads?
int      writeFiles = 0;                           // Perform Reads?
int      deleteFiles = 1;                        // Perform Deletes? (required when numRun>1)
int      randomizeRWOrder = 0;                     // Randomize Create/Read/Write/Delete Order ?
int      minFileSize = 0;                        // Minimum File Size as range
int      maxFileSize = 256000;                     // Maximum File Size as range
int      fsw[10] = {15, 15, 10, 20, 10, 10, 5, 5, 5, 5}; // FileSize Weighting in percentage
int      forkedProcessing = 0;                     // Should it run in forked-mode (ten processes in parallel)?
int      numRuns = 1;                           // How many runs shoudl the program do (requires deleteFiles=1 for numRuns>1)
int      debugLevel = 0;                           // Debugging purposes; max debuglevel = 99
FILE   *statsFile;                              // The actual stats (log) file
int      childId = 0;                           // If in forkedProcessing, the child's own id, otherwise=0
char   *fileContentBuffer;                        // Content to be written out to the files
int      totalFilesCreated = 0;                     // Number of total files created
int      deleteFilesAfterLastRun = 1;               // Should the files be left over after last run?
int      saveFilenames = 1;                        // Internal use only
useconds_t      waitaftercreate = 0;

fileSizeT   filePattern[10];                     // FileSizePattern -- generated
filenamesT   *myFiles = NULL;                     // Contains list of created files
filenamesLT   singleFile;                           // Contains currently used file   


void ldebug(int dLevel, char *p, ...)
{
   va_list arguments;

   if (debugLevel >= dLevel) {
      va_start(arguments, p);
      vprintf(p, arguments);
      va_end(arguments);
   }
}

void Log(char *str, ...)
{
   va_list arguments;

   va_start(arguments, str);
   vfprintf(statsFile, str, arguments);
   va_end(arguments);
   fflush(statsFile);
}

void readConfigFile(char *confFile)
{
   FILE *fp;
   char line[1024+64];
   char key[64];
   char value[1024];
   int   lineNo = 0;

   LDEBUG(5, "READING CONF FILE: [%s]\n", confFile);
   fp = fopen(confFile, "r");
   while (!feof(fp)) {
      if (fgets(line, 1024+64, fp) != NULL) {
         char *tokPos;

         lineNo++;
         line[strlen(line)-1] = '\0';
         tokPos = strstr(line, "#");
         if (tokPos != NULL) {
            *tokPos = '\0';
         }
         tokPos = strstr(line, "=");
         if (tokPos != NULL) {
            char *c;
            strncpy(key, line, tokPos-line);
            key[tokPos-line] = '\0';
            while ((c = strrchr(key, ' ')) != NULL) {
               *c = '\0';
            }
            tokPos++;
            while (*tokPos == ' ') {
               tokPos++;
            }
            strncpy(value, tokPos, strlen(line)-(tokPos-line));
            value[strlen(line)-(tokPos-line)] = '\0';
            while ((c = strrchr(value, ' ')) != NULL) {
               *c = '\0';
            }
            LDEBUG(6, "\t%03d: [%s], [%s]=[%s]\n", lineNo, line, key, value);
            if (strcmp(key, "fspath") == 0) {
               strcpy(fsPath, value);
            } else if (strcmp(key, "logfile") == 0) {
               strcpy(logFile, value);
            } else if (strcmp(key, "numfiles") == 0) {
               sscanf(value, "%d", &numFilesToCreate);
            } else if (strcmp(key, "splitstatsevery") == 0) {
               sscanf(value, "%d", &splitStatsEvery);
            } else if (strcmp(key, "fsblksize") == 0) {
               sscanf(value, "%d", &fsBlkSize);
            } else if (strcmp(key, "minfilesize") == 0) {
               sscanf(value, "%d", &minFileSize);
            } else if (strcmp(key, "maxfilesize") == 0) {
               sscanf(value, "%d", &maxFileSize);
            } else if (strcmp(key, "waitaftercreate") == 0) {
               sscanf(value, "%d", &waitaftercreate);
            } else if (strcmp(key, "fork") == 0) {
               if (strcmp(value, "yes")==0) {
                  forkedProcessing = 1;
               } else {
                  forkedProcessing = 0;
               }
            } else if (strcmp(key, "numruns") == 0) {
               sscanf(value, "%d", &numRuns);
               if (numRuns < 1) {
                  numRuns = 1;
               }
            } else if (strncmp(key, "fsw", 3) == 0) {
               char p[128];
               int index;
               int iVal;
               sscanf(key, "%3s%d", p, &index);
               sscanf(value, "%d", &iVal);
               fsw[index] = iVal;
            } else if (strcmp(key, "readfiles") == 0) {
               if (strcmp(value, "yes") == 0) {
                  readFiles = 1;
               } else {
                  readFiles = 0;
               }
            } else if (strcmp(key, "randomreads") == 0) {
               if (strcmp(value, "yes") == 0) {
                  randomReads = 1;
               } else {
                  randomReads = 0;
               }
            } else if (strcmp(key, "writefiles") == 0) {
               if (strcmp(value, "yes") == 0) {
                  writeFiles = 1;
               } else {
                  writeFiles = 0;
               }
            } else if (strcmp(key, "deletefiles") == 0) {
               if (strcmp(value, "yes") == 0) {
                  deleteFiles = 1;
               } else {
                  deleteFiles = 0;
               }
            } else if (strcmp(key, "deletefilesafterlastrun") == 0) {
               if (strcmp(value, "yes") == 0) {
                  deleteFilesAfterLastRun = 1;
               } else {
                  deleteFilesAfterLastRun = 0;
               }
            } else if (strcmp(key, "randomizeorder") == 0) {
               if (strcmp(value, "yes") == 0) {
                  randomizeRWOrder = 1;
               } else {
                  randomizeRWOrder = 0;
               }
            }
         }
         bzero(line, 1024+64);
      } 
   }
   fclose(fp);
   LDEBUG(5, "DONE READING CONF FILE [%s]\n", confFile);
}

void initFileSizes()
{
   int i, minL, maxL;
   int lowerPad = minFileSize;
   int percentage = fsw[childId];
   int decileMin;
   int decileMax;
   int decileId = childId+1;

   float decileAvg = (maxFileSize - minFileSize) / 10.0;

   if (forkedProcessing == 1) {
      minL = childId;
      maxL = childId+1;
   } else {
      minL = 0;
      maxL = 10;
   }
   for (i=minL; i<maxL; i++) {
      decileId = i+1;
      decileMin = MAX(decileId-1, 0) * decileAvg + minFileSize;
      decileMax = decileId * decileAvg + minFileSize;

      LDEBUG(4, ":initFileSize(ForDecile: %d): dM = %d, dX = %d [m=%d, x=%d]\n", decileId, decileMin, decileMax, minFileSize, maxFileSize);
      filePattern[i].decileMin = decileMin;
      filePattern[i].decileMax = decileMax;
      filePattern[i].decileNumFiles = numFilesToCreate / 100 * fsw[i];
   }
   
}

char *createDirFromNumber(int number, int decile) 
{
   static char path[128];
   char pC[128];
   char *numberStr;
   char *tS;
   int   i;

   bzero(path, 128);
   bzero(pC, 128);
   sprintf(path, "%s/%02d/", fsPath, decile);
   if (mkdir(path, 0777)) {
      // perror(path);
   }

   numberStr = malloc(16);
   sprintf(numberStr, "%010d", number);

   tS = numberStr;

   // /ds/01/10/00/00/00/00
   for (i=0;i<5;i++) {
      pC[0] = *tS++;
      pC[1] = *tS++;
      pC[2] = '\0';
      strcat(pC, "/");
      strcat(path, pC);
      if (mkdir(path, 0777)) {
         // perror(path);
      }
   }

   free(numberStr);
   return path;
}

// /ds/00/00/00/00/00/01/000000000.b
filenamesLT _createOneFileForDecile(char *directory, int decile, int fileNo)
{
   FILE   *fp;
   char   path[255];
   char   *buf;
   int    bufSize;
   int    r;
   double a;
   double t1, t2;

   r = rand();

   a = (r * 1.0 / RAND_MAX);
   bufSize = (a * (filePattern[decile].decileMax - filePattern[decile].decileMin)) + filePattern[decile].decileMin;

   sprintf(path, "%s/%09d.b", directory, fileNo);
   if (bufSize % fsBlkSize == 0) {
      bufSize += ((rand() * 1.0 / RAND_MAX) * 247)+1; // Don't allow alignment on fsBlkSize...
   }

   t1 = timeNano();
   fp=fopen(path, "w");
   fwrite(fileContentBuffer, bufSize, 1, fp);
   fflush(fp);
   fclose(fp);
   t2 = timeNano();

   singleFile.nsecs = t2 - t1;
   strcpy(singleFile.fileName, path);
   singleFile.fileSize = bufSize;

   if (waitaftercreate > 0) {
      usleep(waitaftercreate);
   }
   return singleFile;
}

void _createFilesForDecile(int decile)
{
   double vt1, vt2, vTime, oldVTime;
   double st1, st2;

   double tSec;
   double bps;

   int    lastLogId = 0;
   int    numFiles = 0;

   char   directory[128];
   int    numFilesForDecile;
   int    i;
   int    vtx, vty;
   char   *dir;
   filenamesLT filename;
   double   sumFileSize = 0.0;
   double   sumFileTime = 0.0;
   double   sumFileTimeNS = 0.0;
   int   sumFileSizeNS = 0;

   double minCreateBytesPns = 1000000000.0;
   double maxCreateBytesPns = 0.0;
   double avgCreateBytesPns = 0.0;
   double minCreateFilesPns = 1000000000.0;
   double maxCreateFilesPns = 0.0;
   double avgCreateFilesPns = 0.0;

   bzero(directory, 128);
   numFilesForDecile = numFilesToCreate / 100 * fsw[decile];
   if ((myFiles == NULL) && (saveFilenames == 1)) {
      myFiles = malloc(sizeof(filenamesT) * numFilesForDecile);
   }

   oldVTime = time(NULL);

   vt1 = timeNano();
   Log("%.1lf \"CreateFilesForDecile [decile=%d] START(4 File O/File Created)\" \"-\"\n", vt1, decile);

   st1 = timeNano();
   for (i=0;i<numFilesForDecile;i++) {
      vTime = time(NULL);
      vtx = vTime;
      vty = oldVTime;
      if ((i % splitStatsEvery == 0) || (strlen(directory)==0)) {
         dir = createDirFromNumber(i, decile);
         if (dir) {
            strcpy(directory, dir);
         } else {
            LDEBUG(0, "Could not create directory [%s]\n", directory);
            exit(1);
         }
         oldVTime = vTime;
      }
      filename = _createOneFileForDecile(directory, decile, i);
      if (saveFilenames) {
         if (strlen(filename.fileName)>35) {
            fprintf(stderr,"FATAL: Your path is too long to be managed.\n\tPlease use no more than three (3!) characters for filesystem mount point (e.g. '/ab')\n.");
            fprintf(stderr,"\tAlternatively: use 'read=no', 'write=no', 'delete=no', 'numruns=1', '...lastRun=no' in config-file\n");
            fprintf(stderr,"\tExiting!\n");
            exit(1);
         }
         strcpy(myFiles[totalFilesCreated].fileName, filename.fileName);
         myFiles[totalFilesCreated].fileSize = filename.fileSize;
         myFiles[totalFilesCreated].nsecs = filename.nsecs;
      }
      sumFileTimeNS += filename.nsecs;
      sumFileSizeNS += filename.fileSize;
      totalFilesCreated++;

      tSec = filename.nsecs / NANO;
      sumFileTime = sumFileTime + tSec * 1.0;
      sumFileSize = sumFileSize + filename.fileSize * 1.0;
      bps = filename.fileSize / tSec;
      if (bps < minCreateBytesPns) {
         minCreateBytesPns = bps;
      }
      if (bps > maxCreateBytesPns) {
         maxCreateBytesPns = bps;
      }
      avgCreateBytesPns = (minCreateBytesPns + maxCreateBytesPns)/2;

      if (i>0 && (i % splitStatsEvery == 0)) {
         double avgBytes, avgFiles;
         double fps;
         st2 = timeNano();
         numFiles = i-lastLogId;

         fps = numFiles / (sumFileTimeNS / NANO);
         if (fps < minCreateFilesPns) {
            minCreateFilesPns = fps;
         }
         if (fps > maxCreateFilesPns) {
            maxCreateFilesPns = fps;
         }
         avgCreateFilesPns = (minCreateFilesPns + maxCreateFilesPns)/2;
         avgBytes = sumFileSize / sumFileTime * 1.0;
         avgFiles = i / sumFileTime * 1.0;
//         LDEBUG(0, "sft=%.1lf, sfs=%.1lf, ab = %.1lf, af=%.1lf\n", sumFileTime, sumFileSize, avgBytes, avgFiles);
         Log("%.1lf \"FILES CREATED [decile=%d][numfiles=%d][curfileid=%d][bwritten=%d][totaltime=%.1lf][bps=%.1lf][fps=%.1lf][mBps=%.1lf][xBps=%.1lf][aBps=%.1lf][mFps=%.1lf][xFps=%.1lf][aFps=%.1lf][mFOps=%.1lf][xFOps=%.1lf][aFOps=%.1lf]\" \"%.1lf\"\n", 
                  st2, decile, numFiles, i, sumFileSizeNS, sumFileTimeNS, sumFileSizeNS/(sumFileTimeNS/NANO), fps,
                  minCreateBytesPns, maxCreateBytesPns, avgBytes, 
                  minCreateFilesPns, maxCreateFilesPns, avgFiles,
                  minCreateFilesPns * 4, maxCreateFilesPns * 4, avgFiles * 4,
                  st2-st1);
         lastLogId = i;
         st1 = st2;
         minCreateBytesPns = 1000000000.0;
         maxCreateBytesPns = 0.0;
         avgCreateBytesPns = 0.0;
         sumFileTimeNS = 0.0;
         sumFileSizeNS = 0;
      }
   }
   {
     double avgBytes, avgFiles;
     st2 = timeNano();
     numFiles = i-lastLogId;

     tSec = (st2-st1) / NANO;
     bps = numFiles / tSec;
     if (bps < minCreateFilesPns) {
        minCreateFilesPns = bps;
     }
     if (bps > maxCreateFilesPns) {
        maxCreateFilesPns = bps;
     }
     avgCreateFilesPns = (minCreateFilesPns + maxCreateFilesPns)/2;
     avgBytes = sumFileSize / sumFileTime * 1.0;
     avgFiles = i / sumFileTime * 1.0;
//         LDEBUG(0, "sft=%.1lf, sfs=%.1lf, ab = %.1lf, af=%.1lf\n", sumFileTime, sumFileSize, avgBytes, avgFiles);
     Log("%.1lf \"FILES CREATED [decile=%d][numfiles=%d][curfileid=%d][bwritten=%d][totaltime=%.1lf][bps=%.1lf][fps=%.1lf][mBps=%.1lf][xBps=%.1lf][aBps=%.1lf][mFps=%.1lf][xFps=%.1lf][aFps=%.1lf][mFOps=%.1lf][xFOps=%.1lf][aFOps=%.1lf]\" \"%.1lf\"\n", 
              st2, decile, numFiles, i, sumFileSizeNS, sumFileTimeNS, sumFileSizeNS/(sumFileTimeNS/NANO), bps,
              minCreateBytesPns, maxCreateBytesPns, avgBytes, 
              minCreateFilesPns, maxCreateFilesPns, avgFiles,
              minCreateFilesPns * 4, maxCreateFilesPns * 4, avgFiles * 4,
              st2-st1);
     lastLogId = i;
     st1 = st2;
     minCreateBytesPns = 1000000000.0;
     maxCreateBytesPns = 0.0;
     avgCreateBytesPns = 0.0;
   }

   vt2 = timeNano();
   Log("%.1lf \"CreateFilesForDecile [decile=%d] END\" \"[%.1lf]\"\n", vt2, decile, vt2 - vt1);
}

void doCreateFiles()
{
   double   vt1, vt2;
   int       minD, maxD;
   int       i;

   vt1 = timeNano();
   Log("%.1lf \"CrateFiles START\" \"-\"\n", vt1);
   // START ACTUAL WORKING CODE FROM HERE ONWARDS

   if (forkedProcessing == 1) {
      minD = childId;
      maxD = childId+1;
      myFiles = NULL;
   } else {
      minD = 0;
      maxD = 10;
      if (saveFilenames == 1) {
         myFiles = malloc(sizeof(filenamesT) * numFilesToCreate);
      }
   }
   for (i=minD;i<maxD;i++) {
      _createFilesForDecile(i); 
   }

   // END ACTUAL WORKING CODE AT LATEST HERE
   vt2 = timeNano();
   Log("%.1lf \"CrateFiles END\" \"[%.1lf]\"\n", vt2, vt2-vt1);

}

void readFilesForDecile(int decile)
{
   double vt1, vt2;
   double st1, st2;
   int    i;
   int    fileID; 
   FILE   *fp;
   char   *buf;
   int    numFiles=0;
   int    numFilesInDecile;
   int    lastLogId;
   int    startId=0;
   int    endId=0;
   double minFps=10000000000, maxFps=0, avgFps;
   double minBps=10000000000, maxBps=0, avgBps;
   double timeDiff = 0.0;
   double diffSec = 0.0;
   double fps = 0.0;
   double bps = 0.0;
   int    sumFileSize = 0;
   double sumFileTimeNS = 0.0;

   buf = malloc(maxFileSize*2);

   for (i=0;i<decile;i++) {
         startId += filePattern[i].decileNumFiles; 
   }
   numFilesInDecile = filePattern[decile].decileNumFiles;
   endId = startId + numFilesInDecile;

   vt1 = timeNano();
   Log("%.1lf \"ReadFileForDecile [decile=%d] START (3 FILE O/File Read)\" \"-\"\n", vt1, decile);
   // START ACTUAL WORKING CODE FROM HERE ONWARDS

   st1 = timeNano();
   lastLogId = 0;
   
   for (i=0;i<numFilesInDecile;i++) {
      fileID = ((random() * 1.0 / RAND_MAX) * numFilesInDecile) + startId;
      fp = fopen(myFiles[fileID].fileName, "r");
      if (fp) {
          if (!fread(buf, myFiles[fileID].fileSize, 1, fp)) {
               LDEBUG(5, "Could not read file: [%s] with size [%d]\n", myFiles[fileID].fileName, myFiles[fileID].fileSize);
          }
          fclose(fp);
          sumFileSize += myFiles[fileID].fileSize;
          numFiles++;
      } else {
         LDEBUG(5, "Could not open file: [%s]\n", myFiles[fileID].fileName);
         vt2 = timeNano();
         Log("%.1lf \"!!! COULD NOT READ FILE: [%s] STOPPING FURTHER READ TESTS\" \"-\"", vt2, myFiles[fileID].fileName);
         break;
      }
      if (i>0 && i % splitStatsEvery == 0) {
         st2 = timeNano();
         timeDiff = st2 - st1;
         diffSec = timeDiff / NANO;
         fps = numFiles / diffSec;
         if (fps > maxFps) {
            maxFps = fps;
         }
         if (fps < minFps) {
            minFps = fps;
         }
         bps = sumFileSize / diffSec;   
         if (bps > maxBps) {
            maxBps = bps;
         }
         if (bps < minBps) {
            minBps = bps;
         }
         if ((int)avgBps == 0) {
            avgBps = bps;
         } else {
            avgBps = (avgBps + bps)/2;
         }
         if ((int)avgFps == 0) {
            avgFps = fps;
         } else {
            avgFps = (avgFps + fps)/2;
         }
         Log("%.1lf \"FILES READ [decile=%d][numfiles=%d][curfileid=%d][bread=%d][totaltime=%.1lf][bps=%.1lf][fps=%.1lf][mBps=%.1lf][xBps=%.1lf][aBps=%.1lf][mFps=%.1lf][xFps=%.1lf][aFps=%.1lf][mFOps=%.1lf][xFOps=%.1lf][aFOps=%.1lf]\" \"[%.1lf]\"\n", 
               st2, decile, numFiles, i, sumFileSize, timeDiff,
               bps, fps, minBps, maxBps, avgBps,
               minFps, maxFps, avgFps,
               minFps * 3, maxFps * 3, avgFps * 3,
               timeDiff);
         lastLogId = i;
         st1 = st2;
         sumFileSize = 0.0;
         numFiles = 0;
      }

   }
   {
      st2 = timeNano();
      timeDiff = st2 - st1;
      numFiles = i-lastLogId;
      diffSec = timeDiff / NANO;
      fps = numFiles / diffSec;
      if (fps > maxFps) {
         maxFps = fps;
      }
      if (fps < minFps) {
         minFps = fps;
      }
      bps = sumFileSize / diffSec;   
      if (bps > maxBps) {
         maxBps = bps;
      }
      if (bps < minBps) {
         minBps = bps;
      }
      if ((int)avgBps == 0) {
         avgBps = bps;
      } else {
         avgBps = (avgBps + bps)/2;
      }
      if ((int)avgFps == 0) {
         avgFps = fps;
      } else {
         avgFps = (avgFps + fps)/2;
      }
      Log("%.1lf \"FILES READ [decile=%d][numfiles=%d][curfileid=%d][bread=%d][totaltime=%.1lf][bps=%.1lf][fps=%.1lf][mBps=%.1lf][xBps=%.1lf][aBps=%.1lf][mFps=%.1lf][xFps=%.1lf][aFps=%.1lf][mFOps=%.1lf][xFOps=%.1lf][aFOps=%.1lf]\" \"[%.1lf]\"\n", 
            st2, decile, numFiles, i, sumFileSize, timeDiff,
            bps, fps, minBps, maxBps, avgBps,
            minFps, maxFps, avgFps,
            minFps * 3, maxFps * 3, avgFps * 3,
            timeDiff);
      lastLogId = i;
      st1 = st2;
      sumFileSize = 0.0;
   }

   // END ACTUAL WORKING CODE AT LATEST HERE
   vt2 = timeNano();
   Log("%.1lf \"ReadFileForDecile [decile=%d] END\" \"[%.1lf]\"\n", vt2, decile, vt2-vt1);

   free(buf);
}

void doReadFiles()
{
   int       minD, maxD;
   double   vt1, vt2;
   int       i;

   vt1 = timeNano();
   Log("%.1lf \"ReadFiles START\" \"-\"\n", vt1);
   // START ACTUAL WORKING CODE FROM HERE ONWARDS

   if (forkedProcessing == 1) {
      minD = childId;
      maxD = childId+1;
   } else {
      minD = 0;
      maxD = 10;
   }
   for (i=minD;i<maxD;i++) {
      readFilesForDecile(i); 
   }


   // END ACTUAL WORKING CODE AT LATEST HERE
   vt2 = timeNano();
   Log("%.1lf \"ReadFiles END\" \"[%.1lf]\"\n", vt2, vt2-vt1);

}


void randomReadFilesForDecile(int decile)
{
   double vt1, vt2;
   double st1, st2;
   int    i;
   int    fileID; 
   FILE   *fp;
   char   *buf;
   int    numFiles;
   int    numFilesInDecile;
   int    lastLogId;
   int    startId=0;
   int    endId=0;
   double minFps=10000000000, maxFps=0, avgFps;
   double minBps=10000000000, maxBps=0, avgBps;
   double timeDiff = 0.0;
   double diffSec = 0.0;
   double fps = 0.0;
   double bps = 0.0;
   int    sumFileSize = 0;
   double sumFileTimeNS = 0.0;

   buf = malloc(maxFileSize*2);

   for (i=0;i<decile;i++) {
         startId += filePattern[i].decileNumFiles; 
   }
   numFilesInDecile = filePattern[decile].decileNumFiles;
   endId = startId + numFilesInDecile;

   vt1 = timeNano();
   Log("%.1lf \"RandomReadFileForDecile [decile=%d] START (12 FILE O/File Read)\" \"-\"\n", vt1, decile);
   // START ACTUAL WORKING CODE FROM HERE ONWARDS

   st1 = timeNano();
   lastLogId = 0;
   
   for (i=0;i<numFilesInDecile;i++) {
      fileID = ((random() * 1.0 / RAND_MAX) * numFilesInDecile) + startId;
      fp = fopen(myFiles[fileID].fileName, "r");
      if (fp) {
         int j;
         for (j=0;j<3;j++) {
            int filePos = (random() * 1.0 / RAND_MAX) * (myFiles[fileID].fileSize-1);
            int fLength;
            if (filePos > myFiles[fileID].fileSize) {
               filePos--;
               if (filePos < 0) {
                  filePos = 0;
               }
            }
            fLength = myFiles[fileID].fileSize - filePos - 1;
            if (fLength <1) {
               fLength = 1;
               if (fLength > myFiles[fileID].fileSize) {
                  fLength = 0;
               }
            }
            if (fLength > 0) {
               fseek(fp, filePos, SEEK_SET);
               if (!fread(buf, fLength, 1, fp)) {
                  LDEBUG(5, "Could not read file: [%s] with size [%d], pos=[%d]\n", myFiles[fileID].fileName, fLength, filePos);
                  break;
               } else {
                  sumFileSize += fLength;
               }
            }
            fclose(fp);
            fp = fopen(myFiles[fileID].fileName, "r");
         }
         fclose(fp);
         numFiles++;
      } else {
         LDEBUG(5, "Could not open file: [%s]\n", myFiles[fileID].fileName);
         vt2 = timeNano();
         Log("%.1lf \"!!! COULD NOT READ FILE: [%s] STOPPING FURTHER READ TESTS\" \"-\"", vt2, myFiles[fileID].fileName);
         break;
      }
      if (i>0 && i % splitStatsEvery == 0) {
         st2 = timeNano();
         timeDiff = st2 - st1;
         diffSec = timeDiff / NANO;
         fps = numFiles / diffSec;
         if (fps > maxFps) {
            maxFps = fps;
         }
         if (fps < minFps) {
            minFps = fps;
         }
         bps = sumFileSize / diffSec;   
         if (bps > maxBps) {
            maxBps = bps;
         }
         if (bps < minBps) {
            minBps = bps;
         }
         if ((int)avgBps == 0) {
            avgBps = bps;
         } else {
            avgBps = (avgBps + bps)/2;
         }
         if ((int)avgFps == 0) {
            avgFps = fps;
         } else {
            avgFps = (avgFps + fps)/2;
         }
         Log("%.1lf \"FILES RANDOMREAD [decile=%d][numfiles=%d][curfileid=%d][bread=%d][totaltime=%.1lf][bps=%.1lf][fps=%.1lf][mBps=%.1lf][xBps=%.1lf][aBps=%.1lf][mFps=%.1lf][xFps=%.1lf][aFps=%.1lf][mFOps=%.1lf][xFOps=%.1lf][aFOps=%.1lf]\" \"[%.1lf]\"\n", 
               st2, decile, numFiles, i, sumFileSize, timeDiff,
               bps, fps, minBps, maxBps, avgBps,
               minFps, maxFps, avgFps,
               minFps * 12, maxFps * 12, avgFps * 12,
               timeDiff);
         lastLogId = i;
         st1 = st2;
         sumFileSize = 0;
         numFiles = 0;
      }

   }
   {
      st2 = timeNano();
      timeDiff = st2 - st1;
      numFiles = i-lastLogId;
      diffSec = timeDiff / NANO;
      fps = numFiles / diffSec;
      if (fps > maxFps) {
         maxFps = fps;
      }
      if (fps < minFps) {
         minFps = fps;
      }
      bps = sumFileSize / diffSec;   
      if (bps > maxBps) {
         maxBps = bps;
      }
      if (bps < minBps) {
         minBps = bps;
      }
      if ((int)avgBps == 0) {
         avgBps = bps;
      } else {
         avgBps = (avgBps + bps)/2;
      }
      if ((int)avgFps == 0) {
         avgFps = fps;
      } else {
         avgFps = (avgFps + fps)/2;
      }
      Log("%.1lf \"FILES RANDOMREAD [decile=%d][numfiles=%d][curfileid=%d][bread=%d][totaltime=%.1lf][bps=%.1lf][fps=%.1lf][mBps=%.1lf][xBps=%.1lf][aBps=%.1lf][mFps=%.1lf][xFps=%.1lf][aFps=%.1lf][mFOps=%.1lf][xFOps=%.1lf][aFOps=%.1lf]\" \"[%.1lf]\"\n", 
            st2, decile, numFiles, i, sumFileSize, timeDiff,
            bps, fps, minBps, maxBps, avgBps,
            minFps, maxFps, avgFps,
            minFps * 12, maxFps * 12, avgFps * 12,
            timeDiff);
      lastLogId = i;
      st1 = st2;
      sumFileSize = 0.0;
   }

   // END ACTUAL WORKING CODE AT LATEST HERE
   vt2 = timeNano();
   Log("%.1lf \"RandomReadFileForDecile [decile=%d] END\" \"[%.1lf]\"\n", vt2, decile, vt2-vt1);

   free(buf);
}

void doRandomReadFiles()
{
   int       minD, maxD;
   double   vt1, vt2;
   int       i;

   vt1 = timeNano();
   Log("%.1lf \"RandomReadFiles START\" \"-\"\n", vt1);
   // START ACTUAL WORKING CODE FROM HERE ONWARDS

   if (forkedProcessing == 1) {
      minD = childId;
      maxD = childId+1;
   } else {
      minD = 0;
      maxD = 10;
   }
   for (i=minD;i<maxD;i++) {
      randomReadFilesForDecile(i); 
   }


   // END ACTUAL WORKING CODE AT LATEST HERE
   vt2 = timeNano();
   Log("%.1lf \"RandomReadFiles END\" \"[%.1lf]\"\n", vt2, vt2-vt1);
}

void writeFilesForDecile(int decile)
{
}

void doWriteFiles()
{
   double vt1, vt2;

   vt1 = timeNano();
   Log("%.1lf \"WriteFiles START\" \"-\"\n", vt1);
   // START ACTUAL WORKING CODE FROM HERE ONWARDS




   // END ACTUAL WORKING CODE AT LATEST HERE
   vt2 = timeNano();
   Log("%.1lf \"WriteFiles END\" \"[%.1lf]\"\n", vt2, vt2-vt1);

}


void deleteFilesForDecile(int decile)
{
   double vt1, vt2;
   double st1, st2;
   int    i;
   int    numFiles;
   int    numFilesInDecile;
   int    lastLogId;
   int    startId=0;
   int    endId=0;
   double minFps, maxFps, avgFps;
   double minBps, maxBps, avgBps;
   double timeDiff = 0.0;
   double diffSec = 0.0;
   double fps = 0.0;
   double bps = 0.0;
   int    sumFileSize = 0;
   double sumFileTimeNS = 0.0;

   for (i=0;i<decile;i++) {
         startId += filePattern[i].decileNumFiles; 
   }
   numFilesInDecile = filePattern[decile].decileNumFiles;
   endId = startId + numFilesInDecile;

   vt1 = timeNano();
   Log("%.1lf \"DeleteFilesForDecile [decile=%d] START (1 File O/File Deleted)\" \"-\"\n", vt1, decile);
   // START ACTUAL WORKING CODE FROM HERE ONWARDS

   st1 = timeNano();
   lastLogId = 0;
   minFps = 10000000;
   maxFps = 0.0;
   avgFps = 0.0;
   minBps = 10000000000;
   maxBps = 0.0;
   avgBps = 0.0;
   for (i=0;i<numFilesInDecile;i++) {
      unlink(myFiles[i].fileName);
      sumFileSize += myFiles[i].fileSize;

      if (i>0 && i % splitStatsEvery == 0) {
         st2 = timeNano();
         timeDiff = st2 - st1;
         diffSec = timeDiff / NANO;
         numFiles = i-lastLogId;
         fps = numFiles / diffSec;
         if (fps > maxFps) {
            maxFps = fps;
         }
         if (fps < minFps) {
            minFps = fps;
         }
         bps = sumFileSize / diffSec;   
         if (bps > maxBps) {
            maxBps = bps;
         }
         if (bps < minBps) {
            minBps = bps;
         }
         if ((int)avgBps == 0) {
            avgBps = bps;
         } else {
            avgBps = (avgBps + bps)/2;
         }
         if ((int)avgFps == 0) {
            avgFps = fps;
         } else {
            avgFps = (avgFps + fps)/2;
         }
         Log("%.1lf \"FILES DELETED [decile=%d][numfiles=%d][curfileid=%d][bdeleted=%d][totaltime=%.1lf][bps=%.1lf][fps=%.1lf][mBps=%.1lf][xBps=%.1lf][aBps=%.1lf][mFps=%.1lf][xFps=%.1lf][aFps=%.1lf][mFOps=%.1lf][xFOps=%.1lf][aFOps=%.1lf]\" \"[%.1lf]\"\n", 
               st2, decile, numFiles, i, sumFileSize, timeDiff,
               bps, fps, minBps, maxBps, avgBps,
               minFps, maxFps, avgFps,
               minFps, maxFps, avgFps,
               timeDiff);
         lastLogId = i;
         st1 = st2;
         sumFileSize = 0.0;
      }

   }
   {
      st2 = timeNano();
      timeDiff = st2 - st1;
      diffSec = timeDiff / NANO;
      numFiles = i-lastLogId;
      fps = numFiles / diffSec;
      if (fps > maxFps) {
         maxFps = fps;
      }
      if (fps < minFps) {
         minFps = fps;
      }
      bps = sumFileSize / diffSec;   
      if (bps > maxBps) {
         maxBps = bps;
      }
      if (bps < minBps) {
         minBps = bps;
      }
      if ((int)avgBps == 0) {
         avgBps = bps;
      } else {
         avgBps = (avgBps + bps)/2;
      }
      if ((int)avgFps == 0) {
         avgFps = fps;
      } else {
         avgFps = (avgFps + fps)/2;
      }
      Log("%.1lf \"FILES DELETED [decile=%d][numfiles=%d][curfileid=%d][bdeleted=%d][totaltime=%.1lf][bps=%.1lf][fps=%.1lf][mBps=%.1lf][xBps=%.1lf][aBps=%.1lf][mFps=%.1lf][xFps=%.1lf][aFps=%.1lf][mFO=%.1lf][xFO=%.1lf][aFO=%.1lf]\" \"[%.1lf]\"\n", 
            st2, decile, numFiles, i, sumFileSize, timeDiff,
            bps, fps, minBps, maxBps, avgBps,
            minFps, maxFps, avgFps,
            minFps, maxFps, avgFps,
            timeDiff);
      lastLogId = i;
      st1 = st2;
      sumFileSize = 0.0;
   }

   // END ACTUAL WORKING CODE AT LATEST HERE
   vt2 = timeNano();
   Log("%.1lf \"DeleteFilesForDecile [decile=%d] END\" \"[%.1lf]\"\n", vt2, decile, vt2-vt1);
}

void doDeleteFiles()
{
   int       minD, maxD;
   double   vt1, vt2;
   int       i;

   vt1 = timeNano();
   Log("%.1lf \"DeleteFiles START\" \"-\"\n", vt1);
   // START ACTUAL WORKING CODE FROM HERE ONWARDS

   if (forkedProcessing == 1) {
      minD = childId;
      maxD = childId+1;
   } else {
      minD = 0;
      maxD = 10;
   }
   for (i=minD;i<maxD;i++) {
      deleteFilesForDecile(i); 
   }


   // END ACTUAL WORKING CODE AT LATEST HERE
   vt2 = timeNano();
   Log("%.1lf \"DeleteFiles END\" \"[%.1lf]\"\n", vt2, vt2-vt1);
}


void usage()
{
      printf("Usage:\n");
      printf("\tfiletest -c <configfile> [-d <debuglevel>] \n");
      printf("\n");
      exit(1);
}


int main (int argc, char** argv)
{
   int    i;
   int    forkedId = 0;
   char   confFileName[1024];
   char   ch;

   double v_time, v_time2;
   confFileName[0] = '\0';

   while ((ch = getopt(argc, argv, "hd:c:")) != -1) {
      switch (ch) {
         case 'd':
            sscanf(optarg, "%d", &debugLevel);
         break;
         case 'c':
            strcpy(confFileName, optarg);
         break;
         case 'h':
         default:
            usage();
         }
   }
   argc -= optind;
   argv += optind;
   if (strlen(confFileName) == 0) {
      fprintf(stdout, "\nERROR: Missing config-file.\n");
      usage();
      exit(1);
   } else {   
      readConfigFile(confFileName);
   }

   if ((readFiles == 0) && (randomReads == 0) && (writeFiles == 0) && (deleteFiles == 0) && (numRuns == 1) && (deleteFilesAfterLastRun == 0)) {
      saveFilenames = 0;
   }   

   /*
    * Now let's see if we have to run in parallel with fork()
    * If so, we need to fork ten sub-processes making sure that they themselves
    * don't fork another ten processes EACH.
    *
    * For this, we check for the 'forkedID' variable. So long as forkedId (forkedProcessID) is
    * positive, we are in the main process BECAUSE the forkedId is only set AFTER the child
    * process is forked, i.e. within the childProcess, the forkedId == 0
    *
    * We can do this because we need to fork at least one process.
    * 
    * Once we are finished forking, we reset the ChildID of the parent-process back to '0'
    * as it is NOT a child.
    *
    * The child processes each receive a different CHILDID so that they know what
    * logfile to create, which part of the task to take over.
    *
    * Each child process takes over the job of creating 1/10th of the files. The ChildId
    * provides the info of WHICH 1/10th... (1..9; 0=first 1/10th)
    *
    */
   childId = 0;

   if (forkedProcessing == 1) {
      char logId[4];
      i = 1;
      do {
         childId = i;
         forkedId = fork();
         if (forkedId>0) {
            LDEBUG(10, "Forked Child ID: [%d]\n", forkedId);
         } else {
            LDEBUG(10, "--> I'm a child\n");
         }
         i++;
      } while (forkedId>0 && i<10);
      if (forkedId>0) {
         childId = 0;
      }
      sprintf(logId, ".%02d", childId);
      strcat(logFile, logId);
   }

   /*
    * From here on, all forked process do the same...
    */

   fileContentBuffer = malloc(maxFileSize * 2); // we re-use this buffer in order to avoid malloc/free time penalties

   /*
    * Make sure this buffer contains some random data, otherwise the OS may optimize writes...
    */
   for (i=0;i<maxFileSize*2 -1; i++) {
      char c = (rand() * 1.0 / RAND_MAX) * 255;
      *(fileContentBuffer+i) = c;
   }

   statsFile = fopen(logFile, "w");
   if (!statsFile) {
      printf("ERROR: Could not open lofile [%s] for writing - EXITING!\n", logFile) ;
      exit(1);
   }
   initFileSizes();
   if (numRuns >1) {
      deleteFiles = 1;
   }
   for (i=0; i<numRuns; i++) {
      v_time = timeNano();
      fprintf(statsFile, "%.1lf \"------------------------------------- MARK / RUN: #%d START --------------------------------------\" \"-\"\n", v_time, i+1);
      doCreateFiles();
      if (readFiles) {
         doReadFiles();
      }
      if (writeFiles) {
         doWriteFiles();
      }
      if (randomReads) {
         doRandomReadFiles();
      }
      if (deleteFiles) {
         if (i < numRuns-1 || deleteFilesAfterLastRun == 1) {
            doDeleteFiles();
         }
      }
      v_time2 = timeNano();
      fprintf(statsFile, "%.1lf \"------------------------------------- MARK / RUN: #%d END --------------------------------------\" \"%.1lf\"\n", v_time, i+1, v_time2 - v_time);
   } 
   fclose(statsFile);
   if (myFiles) {
      free(myFiles);
   }
   free(fileContentBuffer);
}


