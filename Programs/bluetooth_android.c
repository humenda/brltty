/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2014 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version. Please see the file LICENSE-GPL for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#include "prologue.h"

#include <errno.h>

#include "io_bluetooth.h"
#include "bluetooth_internal.h"
#include "log.h"
#include "io_misc.h"
#include "system_java.h"

struct BluetoothConnectionExtensionStruct {
  JNIEnv *env;
  jobject connection;
  AsyncHandle inputMonitor;
  int inputPipe[2];
};

static jclass connectionClass = NULL;
static jmethodID connectionConstructor = 0;
static jmethodID closeMethod = 0;
static jmethodID openMethod = 0;
static jmethodID writeMethod = 0;

BluetoothConnectionExtension *
bthNewConnectionExtension (uint64_t bda) {
  BluetoothConnectionExtension *bcx;

  if ((bcx = malloc(sizeof(*bcx)))) {
    memset(bcx, 0, sizeof(*bcx));

    bcx->inputPipe[0] = INVALID_FILE_DESCRIPTOR;
    bcx->inputPipe[1] = INVALID_FILE_DESCRIPTOR;

    if ((bcx->env = getJavaNativeInterface())) {
      if (findJavaClass(bcx->env, &connectionClass, "org/a11y/brltty/android/BluetoothConnection")) {
        if (findJavaConstructor(bcx->env, &connectionConstructor, connectionClass,
                                JAVA_SIG_CONSTRUCTOR(
                                                     JAVA_SIG_LONG // deviceAddress
                                                    ))) {
          jobject localReference = (*bcx->env)->NewObject(bcx->env, connectionClass, connectionConstructor, bda);

          if (!clearJavaException(bcx->env, 0)) {
            jobject globalReference = (*bcx->env)->NewGlobalRef(bcx->env, localReference);

            (*bcx->env)->DeleteLocalRef(bcx->env, localReference);
            localReference = NULL;

            if ((globalReference)) {
              bcx->connection = globalReference;
              return bcx;
            } else {
              logMallocError();
              clearJavaException(bcx->env, 0);
            }
          }
        }
      }
    }

    free(bcx);
  } else {
    logMallocError();
  }

  return NULL;
}

static void
bthCancelInputMonitor (BluetoothConnectionExtension *bcx) {
  if (bcx->inputMonitor) {
    asyncCancelRequest(bcx->inputMonitor);
    bcx->inputMonitor = NULL;
  }
}

void
bthReleaseConnectionExtension (BluetoothConnectionExtension *bcx) {
  bthCancelInputMonitor(bcx);

  if (bcx->connection) {
    if (findJavaInstanceMethod(bcx->env, &closeMethod, connectionClass, "close",
                               JAVA_SIG_METHOD(JAVA_SIG_VOID, ))) {
      (*bcx->env)->CallVoidMethod(bcx->env, bcx->connection, closeMethod);
    }

    (*bcx->env)->DeleteGlobalRef(bcx->env, bcx->connection);
    clearJavaException(bcx->env, 1);
  }

  closeFile(&bcx->inputPipe[0]);
  closeFile(&bcx->inputPipe[1]);

  free(bcx);
}

int
bthOpenChannel (BluetoothConnectionExtension *bcx, uint8_t channel, int timeout) {
  if (pipe(bcx->inputPipe) != -1) {
    if (setBlockingIo(bcx->inputPipe[0], 0)) {
      if (findJavaInstanceMethod(bcx->env, &openMethod, connectionClass, "open",
                                 JAVA_SIG_METHOD(JAVA_SIG_BOOLEAN,
                                                 JAVA_SIG_INT // inputPipe
                                                 JAVA_SIG_INT // channel
                                                 JAVA_SIG_BOOLEAN // secure
                                                ))) {
        jboolean result = (*bcx->env)->CallBooleanMethod(bcx->env, bcx->connection, openMethod,
                                                         bcx->inputPipe[1], channel, JNI_FALSE);

        if (!clearJavaException(bcx->env, 1)) {
          if (result == JNI_TRUE) {
            return 1;
          }
        }

        errno = EIO;
      }
    }

    close(bcx->inputPipe[0]);
    close(bcx->inputPipe[1]);
  } else {
    logSystemError("pipe");
  }

  return 0;
}

int
bthDiscoverChannel (
  uint8_t *channel, BluetoothConnectionExtension *bcx,
  const void *uuidBytes, size_t uuidLength,
  int timeout
) {
  *channel = 0;
  return 1;
}

int
bthMonitorInput (BluetoothConnection *connection, AsyncMonitorCallback *callback, void *data) {
  BluetoothConnectionExtension *bcx = connection->extension;

  bthCancelInputMonitor(bcx);
  if (!callback) return 1;
  return asyncMonitorFileInput(&bcx->inputMonitor, bcx->inputPipe[0], callback, data);
}

int
bthPollInput (BluetoothConnectionExtension *bcx, int timeout) {
  return awaitFileInput(bcx->inputPipe[0], timeout);
}

ssize_t
bthGetData (
  BluetoothConnectionExtension *bcx, void *buffer, size_t size,
  int initialTimeout, int subsequentTimeout
) {
  return readFile(bcx->inputPipe[0], buffer, size, initialTimeout, subsequentTimeout);
}

ssize_t
bthPutData (BluetoothConnectionExtension *bcx, const void *buffer, size_t size) {
  if (findJavaInstanceMethod(bcx->env, &writeMethod, connectionClass, "write",
                             JAVA_SIG_METHOD(JAVA_SIG_BOOLEAN,
                                             JAVA_SIG_ARRAY(JAVA_SIG_BYTE)) // bytes
                                            )) {
    jbyteArray bytes = (*bcx->env)->NewByteArray(bcx->env, size);

    if (bytes) {
      jboolean result;

      (*bcx->env)->SetByteArrayRegion(bcx->env, bytes, 0, size, buffer);
      result = (*bcx->env)->CallBooleanMethod(bcx->env, bcx->connection, writeMethod, bytes);
      (*bcx->env)->DeleteLocalRef(bcx->env, bytes);

      if (!clearJavaException(bcx->env, 1)) {
        if (result == JNI_TRUE) {
          return size;
        }
      }

      errno = EIO;
    } else {
      errno = ENOMEM;
    }
  } else {
    errno = ENOSYS;
  }

  logSystemError("Bluetooth write");
  return -1;
}

char *
bthObtainDeviceName (uint64_t bda, int timeout) {
  char *name = NULL;
  JNIEnv *env = getJavaNativeInterface();

  if (env) {
    static jclass class = NULL;

    if (findJavaClass(env, &class, "org/a11y/brltty/android/BluetoothConnection")) {
      static jmethodID method = 0;

      if (findJavaStaticMethod(env, &method, class, "getName",
                               JAVA_SIG_METHOD(JAVA_SIG_OBJECT(java/lang/String),
                                               JAVA_SIG_LONG // deviceAddress
                                              ))) {
        jstring jName = (*env)->CallStaticObjectMethod(env, class, method, bda);

        if (jName) {
          const char *cName = (*env)->GetStringUTFChars(env, jName, NULL);

          if (cName) {
            if (!(name = strdup(cName))) logMallocError();
            (*env)->ReleaseStringUTFChars(env, jName, cName);
          } else {
            logMallocError();
            clearJavaException(env, 0);
          }

          (*env)->DeleteLocalRef(env, jName);
        } else {
          logMallocError();
          clearJavaException(env, 0);
        }
      }
    }
  }

  return name;
}