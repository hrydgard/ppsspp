plugins {
	id("com.android.application")
	id("com.gladed.androidgitversion")
	id("org.jetbrains.kotlin.android")
}

// Kotlin DSL syntax for configuring the extension
configure<com.gladed.androidgitversion.AndroidGitVersionExtension> {
	codeFormat = "MNNPPBBBB"
	format = "%tag%%-count%%-branch%%-dirty%"
	prefix = "v"  // Only tags beginning with v are considered.
	untrackedIsDirty = false
}

dependencies {
	// 1.6.1 is the newest version we can use that won't complain about minSdk version,
	// and also doesn't collide kotlin versions with com.gladed.androidgitversion.
	// Will replace with a different plugin soon.
	implementation("androidx.appcompat:appcompat:1.6.1")
	implementation("androidx.documentfile:documentfile:1.1.0")
}

android {
	flavorDimensions.add("variant")
	namespace = "org.ppsspp.ppsspp"
	signingConfigs {
		getByName("debug") {
			storeFile = file("debug.keystore")
		}
		create("optimized") {
			storeFile = file("debug.keystore")
		}
		if (project.hasProperty("RELEASE_STORE_FILE")) {
			create("release") {
				storeFile = file(project.property("RELEASE_STORE_FILE") as String)
				storePassword = project.property("RELEASE_STORE_PASSWORD") as String
				keyAlias = project.property("RELEASE_KEY_ALIAS") as String
				keyPassword = project.property("RELEASE_KEY_PASSWORD") as String
			}
		}
	}

	compileSdk = 36
	ndkVersion = "28.2.13676358"

	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_11
		targetCompatibility = JavaVersion.VERSION_11
	}

	lint {
		baseline = file("lint-baseline.xml")
	}

	defaultConfig {
		applicationId = "org.ppsspp.ppsspp"
		// Access the git version info via the extension
		val gitVersion = extensions.getByType<com.gladed.androidgitversion.AndroidGitVersionExtension>()
		if (gitVersion.name() != "unknown") {
			println("Overriding Android Version Name, Code: " + gitVersion.name() + " " + gitVersion.code())
			versionName = gitVersion.name()
			versionCode = gitVersion.code()
		} else {
			println("(not using these:) Android Version Name, Code: " + gitVersion.name() + " " + gitVersion.code())
		}
		file("versionname.txt").writeText(gitVersion.name())
		file("versioncode.txt").writeText(gitVersion.code().toString())

		minSdk = 21
		targetSdk = 36
		if (project.hasProperty("ANDROID_VERSION_CODE") && project.hasProperty("ANDROID_VERSION_NAME")) {
			versionCode = (project.property("ANDROID_VERSION_CODE") as String).toInt()
			versionName = project.property("ANDROID_VERSION_NAME") as String
		}
	}
	buildTypes {
		getByName("debug") {
			isMinifyEnabled = false
			isJniDebuggable = true
			signingConfig = signingConfigs.getByName("debug")
		}
		create("optimized") {
			isMinifyEnabled = false
			isJniDebuggable = true
			signingConfig = signingConfigs.getByName("debug")
		}
		getByName("release") {
			isMinifyEnabled = false
			signingConfig = signingConfigs.getByName("release")
		}
	}
	externalNativeBuild {
		cmake {
			path = file("../CMakeLists.txt")
		}
	}
	packaging {
		jniLibs.useLegacyPackaging = true
	}
	sourceSets {
		getByName("main") {
			manifest.srcFile("AndroidManifest.xml")
			res.setSrcDirs(listOf("res"))
			java.setSrcDirs(listOf("src"))
			aidl.setSrcDirs(listOf("src"))
			resources.setSrcDirs(listOf("src"))
			assets.setSrcDirs(listOf("../assets"))
		}
		create("normal") {
			res.setSrcDirs(listOf("normal/res"))
		}
		create("gold") {
			res.setSrcDirs(listOf("gold/res"))
		}
		create("vr") {
			res.setSrcDirs(listOf("normal/res"))
			manifest.srcFile("VRManifest.xml")
		}
		create("legacy") {
			res.setSrcDirs(listOf("legacy/res"))
		}
	}
	productFlavors {
		create("normal") {
			applicationId = "org.ppsspp.ppsspp"
			dimension = "variant"
			externalNativeBuild {
				cmake {
					// **FIXED**: Using simple "listOf"
					arguments.addAll(listOf(
						"-DANDROID=true",
						"-DANDROID_PLATFORM=android-21",
						"-DANDROID_TOOLCHAIN=clang",
						"-DANDROID_CPP_FEATURES=",
						"-DANDROID_STL=c++_shared"
					))
				}
			}
			ndk {
				abiFilters.addAll(listOf("armeabi-v7a", "arm64-v8a", "x86_64"))
			}
		}
		create("gold") {
			applicationId = "org.ppsspp.ppssppgold"
			dimension = "variant"
			externalNativeBuild {
				cmake {
					arguments.addAll(listOf(
						"-DANDROID=true",
						"-DANDROID_PLATFORM=android-21",
						"-DANDROID_TOOLCHAIN=clang",
						"-DANDROID_CPP_FEATURES=",
						"-DANDROID_STL=c++_shared",
						"-DGOLD=TRUE"
					))
				}
			}
			ndk {
				abiFilters.addAll(listOf("armeabi-v7a", "arm64-v8a", "x86_64"))
			}
		}
		create("legacy") {
			applicationId = "org.ppsspp.ppsspplegacy"
			dimension = "variant"
			targetSdk = 29  // To avoid scoped storage, which is the point of the legacy APK
			externalNativeBuild {
				cmake {
					arguments.addAll(listOf(
						"-DANDROID=true",
						"-DANDROID_PLATFORM=android-21",
						"-DANDROID_TOOLCHAIN=clang",
						"-DANDROID_CPP_FEATURES=",
						"-DANDROID_STL=c++_shared",
						"-DANDROID_LEGACY=TRUE"
					))
				}
			}
			ndk {
				abiFilters.addAll(listOf("armeabi-v7a", "arm64-v8a"))
			}
		}
		create("vr") {
			applicationId = "org.ppsspp.ppssppvr"
			dimension = "variant"
			targetSdk = 29  // To avoid scoped storage, which doesn't work properly on Oculus
			externalNativeBuild {
				cmake {
					arguments.addAll(listOf(
						"-DANDROID=true",
						"-DANDROID_PLATFORM=android-21",
						"-DANDROID_TOOLCHAIN=clang",
						"-DANDROID_CPP_FEATURES=",
						"-DANDROID_STL=c++_shared",
						"-DOPENXR=TRUE",
						"-DANDROID_LEGACY=TRUE"
					))
				}
			}
			ndk {
				abiFilters.addAll(listOf("arm64-v8a"))
			}
		}
	}
	buildFeatures {
		aidl = true
		buildConfig = true
	}
}
androidComponents {
	beforeVariants(selector().all()) { variantBuilder ->
		// **FIXED**: Using simple "setOf"
		val enabledVariants = setOf(
			"normalDebug", "normalOptimized", "normalRelease",
			"goldDebug", "goldRelease",
			"vrDebug", "vrOptimized", "vrRelease",
			"legacyDebug", "legacyOptimized", "legacyRelease"
		)
		variantBuilder.enable = variantBuilder.name in enabledVariants
	}
}

afterEvaluate {
	android.sourceSets.getByName("main").assets.srcDirs.forEach {
		println(it)
	}
}
