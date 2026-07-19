include_guard(GLOBAL)

function(pcg_declare_assimp)
    FetchContent_Declare(
        assimp
        URL https://github.com/assimp/assimp/archive/refs/tags/v6.0.5.tar.gz
        URL_HASH SHA256=edf3749559c2b7d1f758ffb66fc5bec62186221e623b7f2e8969f17ee46ecb6f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endfunction()
