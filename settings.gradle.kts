pluginManagement {
	repositories {
		google()
		mavenCentral()
		gradlePluginPortal()
	}
}
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}

dependencyResolutionManagement {
	repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
	repositories {
		google()
		mavenCentral()
	}
}

rootProject.name = "PPSSPP"
include(":android")

// Moonlight (GameStream) host, for streaming the game to a TV or PC. See MoonlightStream.java.
include(":moonlighthost")
project(":moonlighthost").projectDir = file("ext/moonlight-host-android/moonlighthost")
