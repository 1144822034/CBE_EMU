#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CBE_SCREEN_WIDTH 240
#define CBE_SCREEN_HEIGHT 400

#define MR_MOUSE_DOWN 1
#define MR_MOUSE_UP 2
#define MR_MOUSE_MOVE 3
#define MR_KEY_PRESS 4
#define MR_KEY_RELEASE 5

extern int finalLayerBuffer[];
extern int cbeInit(const char *rootPath);
extern int cbeRun(void);
extern void cbeTaskListRun(void);
extern void cbeShutdown(void);
extern const char *cbeGetCpuInfoText(void);
extern int cbeAndroidInputIsOpen(void);
extern int cbeAndroidInputIsPassword(void);
extern int cbeAndroidInputGetSerial(void);
extern int cbeAndroidInputGetMaxLen(void);
extern int cbeAndroidInputGetInputType(void);
extern const char *cbeAndroidInputGetTextUtf8(void);
extern const char *cbeAndroidInputGetPromptUtf8(void);
extern void cbeAndroidInputSubmitUtf16(const unsigned short *text, int len, int cancelled);
extern void keyEvent(int type, int key);
extern void mouseEvent(int type, int x, int y);

static pthread_mutex_t g_logMutex = PTHREAD_MUTEX_INITIALIZER;
static char g_printBuffer[64 * 1024];
static int g_printBufferLen = 0;

int cbe_android_printf(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_list logAp;
    int len;

    va_start(ap, fmt);
    va_copy(logAp, ap);
    __android_log_vprint(ANDROID_LOG_DEBUG, "CBE_EMU", fmt, logAp);
    va_end(logAp);
    len = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (len < 0)
        return len;

    pthread_mutex_lock(&g_logMutex);
    int copyLen = len;
    if (copyLen >= (int)sizeof(line))
        copyLen = (int)sizeof(line) - 1;
    if (copyLen > 0)
    {
        if (g_printBufferLen + copyLen + 1 >= (int)sizeof(g_printBuffer))
            g_printBufferLen = 0;
        memcpy(g_printBuffer + g_printBufferLen, line, copyLen);
        g_printBufferLen += copyLen;
        g_printBuffer[g_printBufferLen] = 0;
    }
    pthread_mutex_unlock(&g_logMutex);

    return len;
}

const char *cbe_android_get_print_buffer(void)
{
    static char snapshot[64 * 1024];
    pthread_mutex_lock(&g_logMutex);
    memcpy(snapshot, g_printBuffer, g_printBufferLen + 1);
    g_printBufferLen = 0;
    g_printBuffer[0] = 0;
    pthread_mutex_unlock(&g_logMutex);
    return snapshot;
}

static int map_android_button_to_cbe_key(int key)
{
    switch (key)
    {
    case 14:
        return 'w';
    case 15:
        return 's';
    case 16:
        return 'a';
    case 17:
        return 'd';
    case 18:
        return 'f';
    case 20:
        return 'q';
    case 21:
        return 'e';
    case 22:
        return 'z';
    case 23:
        return 'c';
    default:
        return key;
    }
}

JNIEXPORT jint JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_mtk_1open(JNIEnv *env, jobject thiz, jstring sd_path)
{
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, sd_path, NULL);
    int rc = cbeInit(path);
    (*env)->ReleaseStringUTFChars(env, sd_path, path);
    return rc;
}

JNIEXPORT jintArray JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeGetPixels(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    int length = CBE_SCREEN_WIDTH * CBE_SCREEN_HEIGHT;
    jintArray result = (*env)->NewIntArray(env, length);
    if (result == NULL)
        return NULL;
    (*env)->SetIntArrayRegion(env, result, 0, length, finalLayerBuffer);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_startEmu(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    return cbeRun();
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_taskListRun(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    cbeTaskListRun();
}

JNIEXPORT jstring JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_getCpuInfo(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return (*env)->NewStringUTF(env, cbeGetCpuInfoText());
}

JNIEXPORT jstring JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_getPrintBuffer(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return (*env)->NewStringUTF(env, cbe_android_get_print_buffer());
}

JNIEXPORT jboolean JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeIsTextInputOpen(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    return cbeAndroidInputIsOpen() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeGetTextInputSerial(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    return cbeAndroidInputGetSerial();
}

JNIEXPORT jboolean JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeIsTextInputPassword(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    return cbeAndroidInputIsPassword() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeGetTextInputMaxLen(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    return cbeAndroidInputGetMaxLen();
}

JNIEXPORT jint JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeGetTextInputType(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    return cbeAndroidInputGetInputType();
}

JNIEXPORT jstring JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeGetTextInputText(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return (*env)->NewStringUTF(env, cbeAndroidInputGetTextUtf8());
}

JNIEXPORT jstring JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeGetTextInputPrompt(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return (*env)->NewStringUTF(env, cbeAndroidInputGetPromptUtf8());
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeSubmitTextInput(JNIEnv *env, jobject thiz, jstring text, jboolean cancelled)
{
    (void)thiz;
    const jchar *chars = NULL;
    jsize len = 0;
    if (!cancelled && text != NULL)
    {
        chars = (*env)->GetStringChars(env, text, NULL);
        len = (*env)->GetStringLength(env, text);
    }
    cbeAndroidInputSubmitUtf16((const unsigned short *)chars, (int)len, cancelled ? 1 : 0);
    if (chars != NULL)
        (*env)->ReleaseStringChars(env, text, chars);
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_onExit(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    cbeShutdown();
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_touchEvent(JNIEnv *env, jobject thiz, jint touchType, jint x, jint y)
{
    (void)env;
    (void)thiz;
    int mappedType = MR_MOUSE_MOVE;
    if (touchType == 0)
        mappedType = MR_MOUSE_DOWN;
    else if (touchType == 1 || touchType == 3 || touchType == 4)
        mappedType = MR_MOUSE_UP;
    mouseEvent(mappedType, x, y);
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_keyEvent(JNIEnv *env, jobject thiz, jint isPress, jint key)
{
    (void)env;
    (void)thiz;
    keyEvent(isPress ? MR_KEY_PRESS : MR_KEY_RELEASE, map_android_button_to_cbe_key(key));
}
