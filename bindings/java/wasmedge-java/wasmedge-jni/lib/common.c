// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2019-2022 Second State INC

#include "common.h"
#include "jni.h"
#include "wasmedge/wasmedge.h"
#include "ValueType.h"
#include <stdlib.h>
#include <string.h>

bool checkAndHandleException(JNIEnv *env, const char *msg);

void exitWithError(enum ErrorCode error, char *message) { exit(-1); }

void throwNoClassDefError(JNIEnv *env, char *message) {
  jclass exClass;
  char *className = JAVA_LANG_NOCLASSDEFFOUNDERROR;

  exClass = (*env)->FindClass(env, className);

  if (exClass == NULL) {
    exitWithError(JVM_ERROR, ERR_CLASS_NOT_FOUND);
  }
  (*env)->ThrowNew(env, exClass, message);

  exitWithError(JVM_ERROR, ERR_EXCEPTION_THROWN_CLASS_NOT_FOUND);
}

void throwNoSuchMethodError(JNIEnv *env, char *methodName, char *sig) {
  jclass exClass;
  char *className = JAVA_LANG_NOSUCHMETHODERROR;

  char message[1000];

  strcat(message, methodName);
  strcat(message, sig);

  if (exClass == NULL) {
    throwNoClassDefError(env, message);
  }

  (*env)->ThrowNew(env, exClass, methodName);
  exitWithError(JVM_ERROR, ERR_NO_SUCH_METHOD);
}

jclass findJavaClass(JNIEnv *env, char *className) {
  jclass class = (*env)->FindClass(env, className);

  bool hasException = checkAndHandleException(env, ERR_FIND_CLASS);
  if (hasException) {
    return NULL;
  }

  if (class == NULL) {
    throwNoClassDefError(env, className);
  }
  return class;
}

jmethodID findJavaMethod(JNIEnv *env, jclass class, char *methodName,
                         char *sig) {
  jmethodID jmethodId = (*env)->GetMethodID(env, class, methodName, sig);
  return jmethodId;
}

void getClassName(JNIEnv *env, jobject obj, char *buff) {
  jclass cls = (*env)->GetObjectClass(env, obj);

  // First get the class object
  jmethodID mid =
      (*env)->GetMethodID(env, cls, GET_CLASS, VOID_CLASS);
  jobject clsObj = (*env)->CallObjectMethod(env, obj, mid);
  checkAndHandleException(env, ERR_GET_CLASS_NAME);

  // Now get the class object's class descriptor
  cls = (*env)->GetObjectClass(env, clsObj);

  // Find the getName() method on the class object
  mid = (*env)->GetMethodID(env, cls, GET_NAME, VOID_STRING);

  // Call the getName() to get a jstring object back
  jstring strObj = (jstring)(*env)->CallObjectMethod(env, clsObj, mid);
  checkAndHandleException(env, ERR_GET_NAME_FALIED);

  // Now get the c string from the java jstring object
  const char *str = (*env)->GetStringUTFChars(env, strObj, NULL);

  // Print the class name
  strcpy(buff, str);

  // Release the memory pinned char array
  (*env)->ReleaseStringUTFChars(env, strObj, str);
}

long getPointer(JNIEnv *env, jobject obj) {
  jclass cls = (*env)->GetObjectClass(env, obj);

  if (cls == NULL) {
    exitWithError(JVM_ERROR, ERR_CLASS_NOT_FOUND);
  }

  jfieldID fidPointer = (*env)->GetFieldID(env, cls, POINTER, POINTER_TYPE);
  if (fidPointer == NULL) {
    exitWithError(JVM_ERROR, ERR_POINTER_FIELD_NOT_FOUND);
  }
  jlong value = (*env)->GetLongField(env, obj, fidPointer);
  return value;
}

void setPointer(JNIEnv *env, jobject obj, long val) {
  jclass cls = (*env)->GetObjectClass(env, obj);
  jfieldID fidPointer = (*env)->GetFieldID(env, cls, POINTER, POINTER_TYPE);
  (*env)->SetLongField(env, obj, fidPointer, val);
}

void handleWasmEdgeResult(JNIEnv *env, WasmEdge_Result *result) {
  if (!WasmEdge_ResultOK(*result)) {
    char exceptionBuffer[1024];
    sprintf(exceptionBuffer, ERR_TEMPLATE,
            WasmEdge_ResultGetMessage(*result));

    (*env)->ThrowNew(env, (*env)->FindClass(env, JAVA_LANG_EXCEPTION),
                     exceptionBuffer);
  }
}

int getIntVal(JNIEnv *env, jobject val) {
  jclass clazz = (*env)->GetObjectClass(env, val);
  jmethodID methodId = findJavaMethod(env, clazz, GET_VALUE, VOID_INT);

  jint value = (*env)->CallIntMethod(env, val, methodId);
  checkAndHandleException(env, ERR_GET_INT_VALUE);
  return value;
}

long getLongVal(JNIEnv *env, jobject val) {
  jclass clazz = (*env)->GetObjectClass(env, val);

  jmethodID methodId = (*env)->GetMethodID(env, clazz, GET_VALUE, VOID_LONG);
  jlong value = (*env)->CallLongMethod(env, val, methodId);
  return value;
}

long getFloatVal(JNIEnv *env, jobject val) {
  jclass clazz = (*env)->GetObjectClass(env, val);
  jmethodID methodId = findJavaMethod(env, clazz, GET_VALUE, VOID_FLOAT);
  jfloat value = (*env)->CallFloatMethod(env, val, methodId);
  return value;
}

double getDoubleVal(JNIEnv *env, jobject val) {
  jclass clazz = (*env)->GetObjectClass(env, val);
  jmethodID methodId = findJavaMethod(env, clazz, GET_VALUE, VOID_DOUBLE);
  jdouble value = (*env)->CallDoubleMethod(env, val, methodId);
  return value;
}

char *getStringVal(JNIEnv *env, jobject val) {
  jclass clazz = (*env)->GetObjectClass(env, val);

  jmethodID methodId =
      findJavaMethod(env, clazz, GET_VALUE, VOID_STRING);

  jstring value = (jstring)(*env)->CallObjectMethod(env, val, methodId);

  const char *c_str = (*env)->GetStringUTFChars(env, value, NULL);
  size_t len = (*env)->GetStringUTFLength(env, value);
  char *buf = malloc(sizeof(char) * len);

  memcpy(buf, c_str, len);

  (*env)->ReleaseStringUTFChars(env, val, c_str);
  return buf;
}

WasmEdge_ValType *parseValueTypes(JNIEnv *env, jintArray jValueTypes) {
    if (jValueTypes == NULL) {
        return NULL;
    }

    jint len = (*env)->GetArrayLength(env, jValueTypes);
    WasmEdge_ValType *valTypes = (WasmEdge_ValType *)malloc(len * sizeof(WasmEdge_ValType));
    if (valTypes == NULL) {
        // Memory allocation failed
        return NULL;
    }

    jint *elements = (*env)->GetIntArrayElements(env, jValueTypes, NULL);
    if (elements == NULL) {
        // Failed to retrieve array elements
        free(valTypes);
        return NULL;
    }

    for (int i = 0; i < len; ++i) {
        uint32_t typeCode = (uint32_t)elements[i];
        switch (typeCode) {
            case 0x7F:
                valTypes[i] = WasmEdge_ValTypeGenI32();
                break;
            case 0x7E:
                valTypes[i] = WasmEdge_ValTypeGenI64();
                break;
            case 0x7D:
                valTypes[i] = WasmEdge_ValTypeGenF32();
                break;
            case 0x7C:
                valTypes[i] = WasmEdge_ValTypeGenF64();
                break;
            case 0x7B:
                valTypes[i] = WasmEdge_ValTypeGenV128();
                break;
            case 0x70:
                valTypes[i] = WasmEdge_ValTypeGenFuncRef();
                break;
            case 0x6F:
                valTypes[i] = WasmEdge_ValTypeGenExternRef();
                break;
            default:
                break;
        }
    }

    (*env)->ReleaseIntArrayElements(env, jValueTypes, elements, 0);
    return valTypes;
}

bool checkAndHandleException(JNIEnv *env, const char *msg) {
  if ((*env)->ExceptionCheck(env)) {
    jthrowable e = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);

    jclass eclass = (*env)->GetObjectClass(env, e);

    jmethodID mid =
        (*env)->GetMethodID(env, eclass, TO_STRING, VOID_STRING);
    jstring jErrorMsg = (*env)->CallObjectMethod(env, e, mid);
    const char *cMsg = (*env)->GetStringUTFChars(env, jErrorMsg, NULL);

    (*env)->ReleaseStringUTFChars(env, jErrorMsg, cMsg);
    jclass newExcCls = (*env)->FindClass(env, JAVA_LANG_RUNTIMEEXCEPTION);
    if (newExcCls == 0) { /* Unable to find the new exception class, give up. */
      return true;
    }
    (*env)->ThrowNew(env, newExcCls, msg);
    return true;
  }
  return false;
}

void setJavaValueObject(JNIEnv *env, WasmEdge_Value value, jobject j_val) {
    char* str_val;

    // Check the type of the WasmEdge_Value
    if (WasmEdge_ValTypeIsI32(value.Type)) {
        setJavaIntValue(env, value, j_val);
    } else if (WasmEdge_ValTypeIsI64(value.Type) || WasmEdge_ValTypeIsFuncRef(value.Type)) {
        setJavaLongValue(env, value, j_val);
    } else if (WasmEdge_ValTypeIsV128(value.Type)) {
        str_val = u128toa(value.Value); // Ensure proper data access
        setJavaStringValue(env, str_val, j_val);
    } else if (WasmEdge_ValTypeIsF32(value.Type)) {
        setJavaFloatValue(env, value, j_val);
    } else if (WasmEdge_ValTypeIsF64(value.Type)) {
        setJavaDoubleValue(env, value, j_val);
    } else if (WasmEdge_ValTypeIsExternRef(value.Type)) {
        str_val = WasmEdge_ValueGetExternRef(value); // Ensure this function exists
        setJavaStringValue(env, str_val, j_val);
    }
}



jstring WasmEdgeStringToJString(JNIEnv *env, WasmEdge_String wStr) {
  char buf[MAX_BUF_LEN];
  memset(buf, 0, MAX_BUF_LEN);
  WasmEdge_StringCopy(wStr, buf, MAX_BUF_LEN);

  jobject jStr = (*env)->NewStringUTF(env, buf);

  return jStr;
}

jobject CreateJavaArrayList(JNIEnv *env, jint len) {
  jclass listClass = findJavaClass(env, JAVA_UTIL_ARRAYLIST);

  if (listClass == NULL) {
    return NULL;
  }

  jmethodID listConstructor = findJavaMethod(env, listClass, DEFAULT_CONSTRUCTOR, INT_VOID);

  if (listConstructor == NULL) {
    return NULL;
  }

  jobject jList = (*env)->NewObject(env, listClass, listConstructor, len);

  if (jList == NULL) {
    return NULL;
  }

  if (checkAndHandleException(env, ERR_CREATE_JAVA_LIST)) {
    return NULL;
  }
  return jList;
}

bool AddElementToJavaList(JNIEnv *env, jobject jList, jobject ele) {
  jclass listClass = findJavaClass(env, JAVA_UTIL_ARRAYLIST);

  if (listClass == NULL) {
    return false;
  }

  jmethodID addMethod =
      findJavaMethod(env, listClass, ADD_ELEMENT, OBJECT_BOOL);

  return (*env)->CallBooleanMethod(env, jList, addMethod, ele);
}

jobject GetListElement(JNIEnv *env, jobject jList, jint idx) {
  jclass listClass = (*env)->GetObjectClass(env, jList);
  jmethodID getMethod =
      findJavaMethod(env, listClass, GET, INT_OBJECT);

  return (*env)->CallObjectMethod(env, jList, getMethod, idx);
}

jint GetListSize(JNIEnv *env, jobject jList) {

  jclass listClass = (*env)->GetObjectClass(env, jList);
  jmethodID sizeMethod = (*env)->GetMethodID(env, listClass, LIST_SIZE, VOID_INT);
  jint size = (*env)->CallIntMethod(env, jList, sizeMethod);

  return size;
}

WasmEdge_String JStringToWasmString(JNIEnv *env, jstring jstr) {
  uint32_t len = (*env)->GetStringUTFLength(env, jstr);
  const char *strPtr = (*env)->GetStringUTFChars(env, jstr, NULL);

  WasmEdge_String wStr = WasmEdge_StringCreateByBuffer(strPtr, len);

  (*env)->ReleaseStringUTFChars(env, jstr, strPtr);

  return wStr;
}

const char **JStringArrayToPtr(JNIEnv *env, jarray jStrArray) {
  int len = (*env)->GetArrayLength(env, jStrArray);

  const char **ptr = malloc(sizeof(char *));

  for (int i = 0; i < len; i++) {
    jstring jStr = (*env)->GetObjectArrayElement(env, jStrArray, i);
    const char *strPtr = (*env)->GetStringUTFChars(env, jStr, NULL);
    ptr[i] = strPtr;
  }
  return ptr;
}

void ReleaseCString(JNIEnv *env, jarray jStrArray, const char **ptr) {
  int len = (*env)->GetArrayLength(env, jStrArray);

  for (int i = 0; i < len; i++) {
    jstring jStr = (*env)->GetObjectArrayElement(env, jStrArray, i);
    // TODO fixme
    //(*env)->ReleaseStringUTFChars(env, jStr, ptr[i]);
  }
}

jobject WasmEdgeStringArrayToJavaList(JNIEnv *env, WasmEdge_String *wStrList,
                                      int32_t len) {
  jobject strList = CreateJavaArrayList(env, len);

  for (int i = 0; i < len; ++i) {

    jstring jstr = WasmEdgeStringToJString(env, wStrList[i]);
    AddElementToJavaList(env, strList, jstr);
  }
  return strList;
}

