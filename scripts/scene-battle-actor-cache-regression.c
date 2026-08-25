/* Regression for scene kind-3 body Actor lookup in the client cache.
 * This does not start the VM or perform a network request. */

#include <direct.h>
#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    char originalDirectory[512];
    const u32 guestName = 0x00100000u;
    const u32 guestMissingName = 0x00100040u;
    const u32 guestSceneName = 0x00100080u;
    const u32 guestPackageSceneName = 0x001000c0u;
    const u32 package = 0x00100100u;
    const u32 packageNameTable = 0x00100180u;
    const u32 packageIdTable = 0x001001a0u;
    const u32 packageDataTable = 0x001001c0u;
    const u32 packagePlaceholder = 0x00100300u;
    const u16 packageCount = 1;
    const u16 packageFileId = 7;
    /* This background Actor ships in the baseline client cache.  Keep the
     * cache-hit fixture independent of a previous test-map session's dynamic
     * field17/field18 downloads. */
    const char actorName[] = "b_bamboo.actor";
    const char missingActorName[] = "e_missing_body.actor";
    const char packageSceneName[] = "empty.sce";
    const char packageScenePayload[] = "SCE2";
    WIN32_FIND_DATAA sceneFindData;
    HANDLE sceneFind = INVALID_HANDLE_VALUE;
    char sceneName[MAX_PATH] = {0};
    VmReleasedResource *entry = NULL;
    u32 resourceId = 0;
    u32 actorResourceId = 0;
    u32 resultRegister = 0;
    u32 resourceData = 0;
    u32 resourceName = 0;
    char resolvedName[sizeof(actorName)] = {0};

    if (_getcwd(originalDirectory, sizeof(originalDirectory)) == NULL)
    {
        fputs("unable to record client resource working directory\n", stderr);
        return 1;
    }
    /* Run from bin to resolve SDL, but also allow the historical repository
     * root invocation used by non-SDL test runners. */
    if (_chdir("bin") != 0 && _access("JHOnlineData", 0) != 0)
    {
        fputs("unable to enter bin client resource directory\n", stderr);
        return 1;
    }
    sceneFind = FindFirstFileA("JHOnlineData/*.sce", &sceneFindData);
    if (sceneFind == INVALID_HANDLE_VALUE ||
        (sceneFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        sceneFindData.cFileName[0] == 0)
    {
        fputs("unable to locate a client-cached SCE fixture\n", stderr);
        (void)_chdir(originalDirectory);
        return 1;
    }
    snprintf(sceneName, sizeof(sceneName), "%s", sceneFindData.cFileName);
    FindClose(sceneFind);

    if (!vm_resource_is_bare_client_asset(actorName) ||
        !vm_resource_is_bare_client_asset("houzi1.gif") ||
        vm_resource_is_bare_client_asset("JHOnlineData/b_bamboo.actor") ||
        vm_resource_is_bare_client_asset("../b_bamboo.actor") ||
        vm_resource_is_bare_client_asset("b_bamboo.dsh") ||
        !vm_resource_is_bare_client_scene_resource(sceneName) ||
        !vm_resource_is_bare_client_cached_resource(sceneName) ||
        !vm_resource_is_bare_client_scene_resource(packageSceneName) ||
        vm_resource_is_bare_client_cached_resource(packageSceneName) ||
        vm_resource_is_bare_client_scene_resource("JHOnlineData/test.sce") ||
        vm_resource_is_bare_client_scene_resource("../test.sce") ||
        vm_resource_is_bare_client_scene_resource("test.actor"))
    {
        fputs("bare client-cache resource-name boundary failed\n", stderr);
        (void)_chdir(originalDirectory);
        return 1;
    }

    if (uc_open(UC_ARCH_ARM, UC_MODE_ARM, &MTK) != UC_ERR_OK ||
        uc_mem_map(MTK, guestName, 0x1000u, UC_PROT_ALL) != UC_ERR_OK ||
        uc_mem_map(MTK, VM_MALLOC_POOL_ADDRESS, VM_MemoryBlock_SIZE,
                   UC_PROT_ALL) != UC_ERR_OK ||
        uc_mem_write(MTK, guestName, actorName, sizeof(actorName)) != UC_ERR_OK ||
        uc_mem_write(MTK, guestMissingName, missingActorName,
                     sizeof(missingActorName)) != UC_ERR_OK ||
        uc_mem_write(MTK, guestSceneName, sceneName,
                     strlen(sceneName) + 1u) != UC_ERR_OK ||
        uc_mem_write(MTK, guestPackageSceneName, packageSceneName,
                     sizeof(packageSceneName)) != UC_ERR_OK ||
        uc_mem_write(MTK, packagePlaceholder, packageScenePayload,
                     sizeof(packageScenePayload)) != UC_ERR_OK)
    {
        fputs("unable to initialize minimal DreamFactory API test memory\n",
              stderr);
        if (MTK != NULL)
            uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    InitVmMalloc();
    resourceId = vm_DF_GetResourceIDByFileName((int)guestName);
    if (uc_reg_read(MTK, UC_ARM_REG_R0, &resultRegister) != UC_ERR_OK ||
        resultRegister != resourceId)
    {
        fputs("DreamFactory Actor ID lookup did not return its cache ID\n",
              stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    entry = vm_resource_cache_find_by_name(actorName);
    if (resourceId == (u32)-1 || entry == NULL ||
        resourceId != entry->id ||
        strcmp(entry->path, "JHOnlineData/b_bamboo.actor") != 0)
    {
        fprintf(stderr,
                "bare scene Actor did not bypass package ID for JHOnlineData cache "
                "id=%u entry=%p path=%s\n",
                resourceId, (void *)entry,
                entry != NULL ? entry->path : "-");
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    resourceId = vm_DF_DataPackage_GetFileID(0, guestName);
    if (resourceId != entry->id ||
        uc_reg_read(MTK, UC_ARM_REG_R0, &resultRegister) != UC_ERR_OK ||
        resultRegister != resourceId)
    {
        fputs("DataPackage Actor ID lookup bypassed the client cache\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    resourceData = (u32)vm_DF_DataPackage_GetFileByID(0, resourceId);
    if (resourceData == 0)
    {
        fputs("DataPackage Actor resource ID did not load client bytes\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    if (uc_mem_write(MTK, package + 8, &packageCount, sizeof(packageCount)) !=
            UC_ERR_OK ||
        uc_mem_write(MTK, packageIdTable, &packageFileId,
                     sizeof(packageFileId)) != UC_ERR_OK)
    {
        fputs("unable to construct package Actor ID table\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    vm_set_var(package + 12, packageNameTable);
    vm_set_var(package + 16, packageDataTable);
    vm_set_var(package + 20, packageIdTable);
    vm_set_var(package + 24, 0);
    vm_set_var(packageNameTable, guestName);
    vm_set_var(packageDataTable, packagePlaceholder);
    resourceData = (u32)vm_DF_DataPackage_GetFileByID(package, packageFileId);
    if (resourceData == 0 || resourceData == packagePlaceholder ||
        uc_reg_read(MTK, UC_ARM_REG_R0, &resultRegister) != UC_ERR_OK ||
        resultRegister != resourceData)
    {
        fputs("package-local Actor ID bypassed the client cache\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    actorResourceId = resourceId;
    vm_set_var(packageNameTable, guestSceneName);
    resourceId = vm_DF_DataPackage_GetFileID(package, guestSceneName);
    entry = vm_resource_cache_find_by_name(sceneName);
    if (resourceId == (u32)-1 || entry == NULL || resourceId != entry->id ||
        entry->path[0] == 0)
    {
        fputs("package-local SCE ID did not resolve the client cache\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    resourceData = (u32)vm_DF_DataPackage_GetFileByID(package, packageFileId);
    if (resourceData == 0 || resourceData == packagePlaceholder)
    {
        fputs("package-local SCE ID fell back to package bytes\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    resourceData = (u32)vm_DF_DataPackage_GetFile(package, (int)guestSceneName);
    if (resourceData == 0 || resourceData == packagePlaceholder)
    {
        fputs("package-local SCE name lookup fell back to package bytes\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    vm_set_var(packageNameTable, guestMissingName);
    resourceData = (u32)vm_DF_DataPackage_GetFileByID(package, packageFileId);
    if (resourceData != 0 ||
        uc_reg_read(MTK, UC_ARM_REG_R0, &resultRegister) != UC_ERR_OK ||
        resultRegister != 0)
    {
        fputs("missing package-local Actor ID fell back to package bytes\n",
              stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    vm_set_var(packageNameTable, guestPackageSceneName);
    resourceId = vm_DF_DataPackage_GetFileID(package, guestPackageSceneName);
    if (resourceId != packageFileId ||
        uc_reg_read(MTK, UC_ARM_REG_R0, &resultRegister) != UC_ERR_OK ||
        resultRegister != resourceId)
    {
        fputs("package-only SCE ID was incorrectly claimed by the client cache\n",
              stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    resourceData = (u32)vm_DF_DataPackage_GetFileByID(package, packageFileId);
    if (resourceData != packagePlaceholder ||
        uc_reg_read(MTK, UC_ARM_REG_R0, &resultRegister) != UC_ERR_OK ||
        resultRegister != resourceData)
    {
        fputs("package-only SCE ID did not retain its package payload\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    resourceData = (u32)vm_DF_DataPackage_GetFile(
        package, (int)guestPackageSceneName);
    if (resourceData != packagePlaceholder ||
        uc_reg_read(MTK, UC_ARM_REG_R0, &resultRegister) != UC_ERR_OK ||
        resultRegister != resourceData)
    {
        fputs("package-only SCE name did not retain its package payload\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    vm_set_var(packageNameTable, guestName);
    resourceData = (u32)vm_DF_DataPackage_GetFile(0, (int)guestName);
    if (resourceData == 0)
    {
        fputs("DataPackage Actor name lookup did not load client bytes\n", stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    resourceName = (u32)vm_DF_DataPackage_GetFileNameByID(0,
                                                           (int)actorResourceId);
    if (resourceName == 0 ||
        uc_mem_read(MTK, resourceName, resolvedName, sizeof(resolvedName)) !=
            UC_ERR_OK ||
        strcmp(resolvedName, actorName) != 0)
    {
        fputs("DataPackage Actor ID did not resolve back to its cached name\n",
              stderr);
        uc_close(MTK);
        MTK = NULL;
        (void)_chdir(originalDirectory);
        return 1;
    }
    uc_close(MTK);
    MTK = NULL;
    entry = vm_resource_cache_note_name("e_missing_body.actor");
    resourceId = vm_resource_cache_lookup_id("e_missing_body.actor");
    if (entry == NULL || entry->path[0] != 0 || resourceId != (u32)-1)
    {
        fputs("name-only Actor cache entry masked a missing client resource\n",
              stderr);
        (void)_chdir(originalDirectory);
        return 1;
    }
    snprintf(entry->path, sizeof(entry->path), "%s",
             "JHOnlineData/e_missing_body.actor");
    resourceId = vm_resource_cache_load_by_name("e_missing_body.actor");
    if (resourceId != 0)
    {
        fputs("stale Actor cache path masked a missing client resource\n",
              stderr);
        (void)_chdir(originalDirectory);
        return 1;
    }
    if (_chdir(originalDirectory) != 0)
    {
        fputs("unable to restore working directory\n", stderr);
        return 1;
    }
    puts("scene battle Actor cache regression passed");
    return 0;
}
