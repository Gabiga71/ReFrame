set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

# A Revive maga v142-vel fordul (lasd PlatformToolset a .vcxproj fajlokban), ezert a
# fuggosegeket is azzal epitjuk, ahelyett hogy a VS2026 alapertelmezett v145-jevel
# epult statikus libeket linkelnenk be es a verziok kozti binaris kompatibilitasra
# hagyatkoznank. Statikus CRT mellett ez a kockazat nem eri meg.
set(VCPKG_PLATFORM_TOOLSET v142)
