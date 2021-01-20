#pragma once

#include <fs/filesystem.h>

namespace fs{

    class FsVolume{
    public:
        volume_id_t volumeID;
        FsNode* mountPoint;
        DirectoryEntry mountPointDirent;
    };    

    class LinkVolume : public FsVolume{
    public:
        LinkVolume(FsVolume* link, char* name){
            strcpy(mountPointDirent.name, name);
            mountPointDirent.node = link->mountPoint;
            mountPointDirent.flags = link->mountPointDirent.flags;
            mountPointDirent.node->nlink++;
        }

        LinkVolume(FsNode* link, char* name){
            strcpy(mountPointDirent.name, name);
            mountPointDirent.node = link;
            mountPointDirent.flags = DT_DIR;
            mountPointDirent.node->nlink++;
        }
    };
}