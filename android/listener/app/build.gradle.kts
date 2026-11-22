plugins {
    id("com.android.application")
}

android {
    namespace = "com.lowpoplabs.petlistener"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.lowpoplabs.petlistener"
        minSdk = 31         // Android 12: the first with BLUETOOTH_SCAN and "never for location"
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
    }

    // Kotlin compiles to the same level without being told.
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
