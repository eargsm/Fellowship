includes("lib/commonlibsse-ng")

set_project("Fellowship")
set_version("1.4.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target("Fellowship")
    add_rules("commonlibsse-ng.plugin", {
        name = "Fellowship",
        author = "Rain",
        description = "Travel with several followers using the vanilla follower dialogue"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    add_includedirs("extern/SKSEMenuFramework")
    set_pcxxheader("src/pch.h")
