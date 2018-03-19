#pragma once

int init_vfs_changes(void) __init;
void cleanup_vfs_changes(void);

void vfs_changed(int act, const char* root, const char* src, const char* dst);
