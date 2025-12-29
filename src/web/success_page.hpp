#pragma once

#include <string_view>

inline constexpr std::string_view SUCCESS_PAGE_HTML = R"HTML(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="utf-8">
        <title>Saved</title>
    </head>

    <body>
        <h3>Credentials saved.</h3>
        <p>Device will connect and reboot.</p>
    </body>

    </html>
    )HTML";
