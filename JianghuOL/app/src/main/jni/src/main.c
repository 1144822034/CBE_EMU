#include <jni.h>

const char* sdPath;

extern int finalLayerBuffer[];
extern void RunArmProgram(unsigned int);

_Noreturn extern void ScreenRenderThread();
extern int mtkMain();
extern char* dumpCpuInfo();
extern char* getPrintBuffer();
extern void onExit();
extern void simTouchEvent(int touchType,int x,int y);
extern void simKeyEvent(int isPress,int keyCode);

JNIEXPORT
jint Java_com_xiaoxiao_mt6252simulator_MainActivity_mtk_1open(JNIEnv *env, jobject thiz, jstring sd_path) {
    sdPath = (*env)->GetStringUTFChars(env, sd_path, NULL);
    mtkMain();
    return 0;
}

JNIEXPORT jintArray JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_nativeGetPixels(JNIEnv *env, jobject thiz) {
    // 创建 Java int[] 对象
    int length = 240*320;
    jintArray result = (*env)->NewIntArray(env, length);
    if (result == NULL) return NULL; // 内存不足
    // 拷贝数据到 Java 数组中
    (*env)->SetIntArrayRegion(env, result, 0, length, finalLayerBuffer);
    return result;
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_startEmu(JNIEnv *env, jobject thiz) {
    RunArmProgram(0x08000000);
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_taskListRun(JNIEnv *env, jobject thiz) {
    //执行屏幕渲染等任务
    ScreenRenderThread();
}

JNIEXPORT jstring JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_getCpuInfo(JNIEnv *env, jobject thiz) {
    char* s = dumpCpuInfo();
    return (*env)->NewStringUTF(env,s);
}

JNIEXPORT jstring JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_getPrintBuffer(JNIEnv *env, jobject thiz) {
    const char* ptr = getPrintBuffer();
    return (*env)->NewStringUTF(env, ptr);
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_onExit(JNIEnv *env, jobject thiz) {
    onExit();
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_touchEvent(JNIEnv *env, jobject thiz,jint touchType, jint x,jint y) {
    simTouchEvent(touchType,x,y);
}

JNIEXPORT void JNICALL
Java_com_xiaoxiao_mt6252simulator_MainActivity_keyEvent(JNIEnv *env, jobject thiz,jint isPress, jint key) {
    simKeyEvent(isPress,key);
}
