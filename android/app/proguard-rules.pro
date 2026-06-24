# Keep kotlinx.serialization generated serializers.
-keepattributes *Annotation*, InnerClasses
-dontnote kotlinx.serialization.**
-keepclassmembers class com.wifimap.radar.** {
    *** Companion;
}
-keepclasseswithmembers class com.wifimap.radar.** {
    kotlinx.serialization.KSerializer serializer(...);
}
