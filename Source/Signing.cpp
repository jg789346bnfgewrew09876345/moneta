/*
____________________________________________________________________________________________________
| _______  _____  __   _ _______ _______ _______                                                   |
| |  |  | |     | | \  | |______    |    |_____|                                                   |
| |  |  | |_____| |  \_| |______    |    |     |                                                   |
|__________________________________________________________________________________________________|
| Moneta ~ Usermode memory scanner & malware hunter                                                |
|--------------------------------------------------------------------------------------------------|
| https://www.forrest-orr.net/post/masking-malicious-memory-artifacts-part-ii-insights-from-moneta |
|--------------------------------------------------------------------------------------------------|
| Author: Forrest Orr - 2020                                                                       |
|--------------------------------------------------------------------------------------------------|
| Contact: forrest.orr@protonmail.com                                                              |
|--------------------------------------------------------------------------------------------------|
| Licensed under GNU GPLv3                                                                         |
|__________________________________________________________________________________________________|
| ## Features                                                                                      |
|                                                                                                  |
| ~ Query the memory attributes of any accessible process(es).                                     |
| ~ Identify private, mapped and image memory.                                                     |
| ~ Correlate regions of memory to their underlying file on disks.                                 |
| ~ Identify PE headers and sections corresponding to image memory.                                |
| ~ Identify modified regions of mapped image memory.                                              |
| ~ Identify abnormal memory attributes indicative of malware.                                     |
| ~ Create memory dumps of user-specified memory ranges                                            |
| ~ Calculate memory permission/type statistics                                                    |
|__________________________________________________________________________________________________|

*/

#include "StdAfx.h"
#include "Signing.h"

using namespace std;

#pragma comment (lib, "Wintrust.lib")
#pragma comment (lib, "Crypt32.lib")

bool VerifyEmbeddedSignature(const wchar_t *FilePath) {
    LONG lStatus;
    uint32_t dwLastError;
    bool bSigned = false;
    WINTRUST_FILE_INFO FileData = { 0 };
    GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA WinTrustData = { 0 };

    FileData.cbStruct = sizeof(WINTRUST_FILE_INFO);
    FileData.pcwszFilePath = FilePath;
    FileData.hFile = nullptr;
    FileData.pgKnownSubject = nullptr;

    WinTrustData.cbStruct = sizeof(WinTrustData);
    WinTrustData.pPolicyCallbackData = nullptr;
    WinTrustData.pSIPClientData = nullptr;
    WinTrustData.dwUIChoice = WTD_UI_NONE;
    WinTrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    WinTrustData.dwUnionChoice = WTD_CHOICE_FILE;
    WinTrustData.dwStateAction = WTD_STATEACTION_VERIFY;
    WinTrustData.hWVTStateData = nullptr;
    WinTrustData.pwszURLReference = nullptr;
    WinTrustData.dwUIContext = 0;
    WinTrustData.pFile = &FileData;

    lStatus = WinVerifyTrust(nullptr, &WVTPolicyGUID, &WinTrustData);

    switch (lStatus) {
        case ERROR_SUCCESS: {
            bSigned = true;
            break;
        }
        case TRUST_E_NOSIGNATURE: {
            dwLastError = GetLastError();
            break;
        }
        default: break;
    }

    WinTrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &WVTPolicyGUID, &WinTrustData);

    return bSigned;
}

wchar_t* GetPeCatalogIssuer(const wchar_t* FilePath) {
    HCATADMIN hCatalogContext = nullptr;
	wchar_t* CertIssuerStr = nullptr;

    if (CryptCATAdminAcquireContext(&hCatalogContext, nullptr, 0)) {
		HANDLE hFile;
		uint32_t dwHashSize = 0;

        if ((hFile = CreateFileW(FilePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)) != INVALID_HANDLE_VALUE) {
            if (CryptCATAdminCalcHashFromFileHandle(hFile, reinterpret_cast<PDWORD>(&dwHashSize), nullptr, 0) && dwHashSize != 0) {
				unique_ptr<uint8_t[]> HashBuf = make_unique<uint8_t[]>(dwHashSize);

                if (CryptCATAdminCalcHashFromFileHandle(hFile, reinterpret_cast<PDWORD>(&dwHashSize), HashBuf.get(), 0)) {
					HCATINFO hTargetCatalog;

                    if ((hTargetCatalog = CryptCATAdminEnumCatalogFromHash(hCatalogContext, HashBuf.get(), dwHashSize, 0, nullptr)) != nullptr) {
						CATALOG_INFO CatalogInfo = { 0 };
						CatalogInfo.cbStruct = sizeof(CATALOG_INFO);

                        if (CryptCATCatalogInfoFromContext(hTargetCatalog, &CatalogInfo, 0)) { // Catalog file at CatalogInfo.wszCatalogFile
							HCERTSTORE hCertStore = nullptr;
							HCRYPTMSG hMsg = nullptr;
							uint32_t dwEncoding = 0, dwContentType = 0, dwFormatType = 0;

                            if (CryptQueryObject(CERT_QUERY_OBJECT_FILE, CatalogInfo.wszCatalogFile, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0, reinterpret_cast<PDWORD>(&dwEncoding), reinterpret_cast<PDWORD>(&dwContentType), reinterpret_cast<PDWORD>(&dwFormatType), &hCertStore, &hMsg, nullptr)) {
								uint32_t dwSignerInfoSize = 0;

								if (CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, reinterpret_cast<PDWORD>(&dwSignerInfoSize)) && dwSignerInfoSize) {
									unique_ptr<uint8_t[]> SignerInfo = make_unique<uint8_t[]>(dwSignerInfoSize);
									
                                    if (CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, static_cast<void*>(SignerInfo.get()), reinterpret_cast<PDWORD>(&dwSignerInfoSize))) {
										CERT_INFO CertInfo = { 0 };
										PCCERT_CONTEXT CertCtx;

                                        CertInfo.Issuer = reinterpret_cast<PCMSG_SIGNER_INFO>(SignerInfo.get())->Issuer;
                                        CertInfo.SerialNumber = reinterpret_cast<PCMSG_SIGNER_INFO>(SignerInfo.get())->SerialNumber;

                                        if ((CertCtx = CertFindCertificateInStore(hCertStore, (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING), 0, CERT_FIND_SUBJECT_CERT, static_cast<void*>(&CertInfo), nullptr)) != nullptr) {
											uint32_t dwCertNameLength;

                                            if ((dwCertNameLength = CertGetNameStringW(CertCtx, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, nullptr, nullptr, 0))) {
												unique_ptr<wchar_t[]> CertIssuerBuf = make_unique<wchar_t[]>(dwCertNameLength + 1);

                                                if (CertGetNameStringW(CertCtx, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, nullptr, CertIssuerBuf.get(), dwCertNameLength + 1)) { // Char count must include NULL terminator according to MSDN
													CertIssuerStr = CertIssuerBuf.get();
													CertIssuerBuf.release();
												}
                                            }

                                            CertFreeCertificateContext(CertCtx);
                                        }
                                    }
                                }

                                CryptMsgClose(hMsg);
                                CertCloseStore(hCertStore, CERT_CLOSE_STORE_CHECK_FLAG);
                            }
                        }

                        CryptCATAdminReleaseCatalogContext(hCatalogContext, hTargetCatalog, 0);
                    }
                }
            }

            CloseHandle(hFile);
        }

        CryptCATAdminReleaseContext(hCatalogContext, 0);
    }

    return CertIssuerStr;
}

bool VerifyCatalogSignature(const wchar_t* FilePath) {
    wchar_t* CertIssuer;

    if ((CertIssuer = GetPeCatalogIssuer(FilePath)) != nullptr) {
        delete[] CertIssuer;
		return true;
    }

    return false;
}

Signing_t CheckSigning(const wchar_t* TargetFilePath) {
    if (!VerifyEmbeddedSignature(TargetFilePath)) {
        if (!VerifyCatalogSignature(TargetFilePath)) {
            return Signing_t::Unsigned;
        }
        else {
            return Signing_t::Catalog;
        }
    }
    else {
        return Signing_t::Embedded;
    }
}

const wchar_t* TranslateSigningLevel(uint32_t dwSigningLevel) {
    switch (dwSigningLevel) {
        case 0: return L"Unchecked";
        case 1: return L"Unsigned";
        case 4: return L"Authenticode";
        case 6: return L"Store";
        case 7: return L"Anti-malware";
        case 8: return L"Microsoft";
        case 12: return L"Windows";
        case 14: return L"Windows TCB";
        default: return L"?";
    }
}

const wchar_t* TranslateSigningType(Signing_t Type) {
    switch (Type) {
        case Signing_t::Catalog: return L"Catalog";
        case Signing_t::Embedded: return L"Embedded";
        case Signing_t::Unsigned: return L"Unsigned";
        default: return L"?";
    }
}