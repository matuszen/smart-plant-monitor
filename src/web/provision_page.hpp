#pragma once

#include <string_view>

inline constexpr std::string_view PROVISION_PAGE_HTML = R"HTML(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="utf-8">
        <title>Smart Plant Monitor Wi-Fi</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
            body {
                font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
                max-width: 520px;
                margin: 0 auto;
                padding: 16px;
                background: #f6f8fb;
                color: #0b1a2c;
            }

            h2 {
                margin-top: 0;
            }

            .card {
                background: #fff;
                border: 1px solid #d6dce5;
                border-radius: 10px;
                padding: 14px;
                margin-bottom: 14px;
                box-shadow: 0 1px 3px rgba(0, 0, 0, 0.05);
            }

            .label {
                font-size: 12px;
                color: #5a6472;
                text-transform: uppercase;
                letter-spacing: 0.04em;
                margin-bottom: 6px;
            }

            .value {
                font-weight: 600;
            }

            button,
            input[type=submit] {
                margin-top: 10px;
                padding: 10px 14px;
                border: none;
                border-radius: 8px;
                background: #0b6cf0;
                color: #fff;
                font-weight: 600;
                cursor: pointer;
            }

            button.secondary {
                background: #0d1b2a;
            }

            input[type=text],
            input[type=password] {
                width: 100%;
                padding: 10px;
                border: 1px solid #cdd5e1;
                border-radius: 8px;
                margin: 6px 0 12px 0;
            }

            hr {
                border: none;
                border-top: 1px solid #dfe5ef;
                margin: 18px 0;
            }

            #status {
                margin-top: 8px;
                color: #0b6cf0;
                font-weight: 600;
            }

            .notice {
                color: #8a95a3;
                font-size: 13px;
            }
        </style>
        <script>
            function useStored() {
                fetch('/use-stored', { method: 'POST' }).then(() => {
                    const status = document.getElementById('status');
                    if (status) { status.innerText = 'Connecting with stored network...'; }
                });
            }
        </script>
    </head>

    <body>
        <h2>Smart Plant Monitor</h2>
        <div class="card">
            <div class="label">Current Wi-Fi</div>
            <div class="value" id="current">{{CURRENT_BLOCK}}</div>
            <div id="status"></div>
        </div>
        <hr>
        <div class="card">
            <div class="label">Replace credentials</div>
            <form method="POST" action="/">
                <input name="ssid" type="text" maxlength="32" placeholder="SSID" required>
                <input name="pass" type="password" maxlength="64" placeholder="Password">
                <input type="submit" value="Save & Connect">
            </form>
            <div class="notice">If password is empty, an open network will be used.</div>
        </div>
    </body>

    </html>
    )HTML";
