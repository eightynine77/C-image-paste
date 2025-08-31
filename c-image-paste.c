#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus")

using namespace Gdiplus;

static void print_failed_and_exit(void) {
    fputs("pasting image from clipboard failed\n", stderr);
    exit(1);
}

static int make_temp_name(const char* ext, char* out, size_t outlen) {
    char tmpPath[MAX_PATH];
    char tmpFile[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmpPath)) return 0;
    if (!GetTempFileNameA(tmpPath, "cb", 0, tmpFile)) return 0;
    char newName[MAX_PATH];
    strncpy(newName, tmpFile, MAX_PATH - 1);
    newName[MAX_PATH - 1] = '\0';
    char* dot = strrchr(newName, '.');
    if (!dot) return 0;
    snprintf(dot, MAX_PATH - (dot - newName), "%s", ext);
    MoveFileA(tmpFile, newName);
    strncpy(out, newName, outlen - 1);
    out[outlen - 1] = '\0';
    return 1;
}

static CLSID GetEncoderClsid(const WCHAR* format) {
    CLSID clsid = { 0 };
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return clsid;
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (!pImageCodecInfo) return clsid;
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0 ||
            wcscmp(pImageCodecInfo[j].FormatDescription, format) == 0) {
            clsid = pImageCodecInfo[j].Clsid;
            break;
        }
    }
    free(pImageCodecInfo);
    return clsid;
}

static void to_wide(const char* in, WCHAR* out, int outlen) {
    MultiByteToWideChar(CP_ACP, 0, in, -1, out, outlen);
}

int main(void) {
    if (!OpenClipboard(NULL)) {
        print_failed_and_exit();
    }

    UINT pngFmt = RegisterClipboardFormatA("PNG");
    HGLOBAL hdata = NULL;
    char outpath[MAX_PATH] = { 0 };
    int wrote = 0;

    if (pngFmt && IsClipboardFormatAvailable(pngFmt)) {
        hdata = GetClipboardData(pngFmt);
        if (!hdata) { CloseClipboard(); print_failed_and_exit(); }
        void* ptr = GlobalLock(hdata);
        if (!ptr) { GlobalUnlock(hdata); CloseClipboard(); print_failed_and_exit(); }
        SIZE_T sz = GlobalSize(hdata);
        if (sz == 0) { GlobalUnlock(hdata); CloseClipboard(); print_failed_and_exit(); }

        if (!make_temp_name(".png", outpath, sizeof(outpath))) {
            GlobalUnlock(hdata); CloseClipboard(); print_failed_and_exit();
        }
        FILE* f = fopen(outpath, "wb");
        if (!f) { GlobalUnlock(hdata); CloseClipboard(); print_failed_and_exit(); }
        if (fwrite(ptr, 1, sz, f) != sz) { fclose(f); GlobalUnlock(hdata); CloseClipboard(); print_failed_and_exit(); }
        fclose(f);
        GlobalUnlock(hdata);
        wrote = 1;
    }
    else {
        HBITMAP hBitmap = NULL;

        if (IsClipboardFormatAvailable(CF_DIB)) {
            hdata = GetClipboardData(CF_DIB);
            if (!hdata) { CloseClipboard(); print_failed_and_exit(); }
            void* ptr = GlobalLock(hdata);
            if (!ptr) { GlobalUnlock(hdata); CloseClipboard(); print_failed_and_exit(); }

            BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)ptr;
            if (!bih) { GlobalUnlock(hdata); CloseClipboard(); print_failed_and_exit(); }

            DWORD colors = 0;
            if (bih->biClrUsed) colors = bih->biClrUsed;
            else {
                if (bih->biBitCount <= 8) colors = 1u << bih->biBitCount;
                else colors = 0;
            }
            DWORD headerSize = sizeof(BITMAPINFOHEADER) + colors * sizeof(RGBQUAD);
            BYTE* bits = (BYTE*)ptr + headerSize;

            HDC hdc = GetDC(NULL);
            if (!hdc) { GlobalUnlock(hdata); CloseClipboard(); print_failed_and_exit(); }

            HBITMAP hbm = CreateDIBitmap(hdc, bih, CBM_INIT, bits, (BITMAPINFO*)bih, DIB_RGB_COLORS);
            ReleaseDC(NULL, hdc);

            GlobalUnlock(hdata);
            if (!hbm) { CloseClipboard(); print_failed_and_exit(); }
            hBitmap = hbm;
        }
        else if (IsClipboardFormatAvailable(CF_BITMAP)) 
        {
            HBITMAP hbmp = (HBITMAP)GetClipboardData(CF_BITMAP);
            if (!hbmp) 
            { 
                CloseClipboard(); print_failed_and_exit(); 
            }
            hBitmap = hbmp;
        }
        else {
            CloseClipboard();
            print_failed_and_exit();
        }

        GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
            CloseClipboard();
            print_failed_and_exit();
        }

        Bitmap* gdiBmp = Bitmap::FromHBITMAP(hBitmap, NULL);
        if (!gdiBmp) {
            GdiplusShutdown(gdiplusToken);
            CloseClipboard();
            print_failed_and_exit();
        }

        if (!make_temp_name(".png", outpath, sizeof(outpath))) {
            delete gdiBmp;
            GdiplusShutdown(gdiplusToken);
            CloseClipboard();
            print_failed_and_exit();
        }

        WCHAR wpath[MAX_PATH];
        to_wide(outpath, wpath, MAX_PATH);

        CLSID pngClsid;
        UINT numEnc = 0, sizeEnc = 0;
        GetImageEncodersSize(&numEnc, &sizeEnc);
        if (sizeEnc == 0) {
            delete gdiBmp;
            GdiplusShutdown(gdiplusToken);
            CloseClipboard();
            print_failed_and_exit();
        }
        ImageCodecInfo* pCodecInfo = (ImageCodecInfo*)malloc(sizeEnc);
        if (!pCodecInfo) {
            delete gdiBmp;
            GdiplusShutdown(gdiplusToken);
            CloseClipboard();
            print_failed_and_exit();
        }
        GetImageEncoders(numEnc, sizeEnc, pCodecInfo);
        bool found = false;
        for (UINT i = 0; i < numEnc; ++i) {
            if (wcscmp(pCodecInfo[i].MimeType, L"image/png") == 0) {
                pngClsid = pCodecInfo[i].Clsid;
                found = true;
                break;
            }
        }
        free(pCodecInfo);
        if (!found) {
            delete gdiBmp;
            GdiplusShutdown(gdiplusToken);
            CloseClipboard();
            print_failed_and_exit();
        }

        Status s = gdiBmp->Save(wpath, &pngClsid, NULL);
        delete gdiBmp;
        GdiplusShutdown(gdiplusToken);
        if (s != Ok) {
            DeleteFileA(outpath);
            CloseClipboard();
            print_failed_and_exit();
        }

        wrote = 1;
    }

    CloseClipboard();

    if (wrote) {
        printf("%s\n", outpath);
        fflush(stdout);
        return 0;
    }
    else {
        print_failed_and_exit();
    }
    return 0;
}
