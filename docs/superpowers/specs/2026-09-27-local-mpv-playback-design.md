# Local mpv playback design

The anime detail page offers a per-file “用 mpv 播放” action. This is not browser video streaming: the same-machine backend launches the configured mpv executable with the selected media path. It matches section 7.1 of the project specification and the user's mpv preference. Browser-embedded playback and remote player agents remain out of scope.

`POST /api/media/{id}/play` accepts no body or query path. The backend loads a media record, prefers its organized library path when present, otherwise uses its source path, and permits playback only when the resolved regular file remains inside the corresponding configured library, qB download, or external-import root. It does not move or modify media. A configured, existing mpv executable is required. The process wrapper passes executable and arguments directly, never through a shell. It starts detached so the request returns promptly.

The endpoint returns a stable JSON result or error code. It rejects non-local browser origins to limit cross-site launch requests; non-browser clients without an Origin header remain usable on the loopback listener. UI shows a busy state and any API error next to the media list. Tests cover file/root selection and rejected paths, launch arguments, and the page action. No real media is played by automated tests.
