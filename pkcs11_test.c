/*
 *  pkcs11_test.c
 *
 *  Originally derived from pkcs11_test from KeychainToken:
 *
 *  Created by Jay Kline on 6/24/09.
 *  Copyright 2009,2016
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "pkcs11_test.h"
#include "config.h"

#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

/*
 * Dump one or more attributes of an object
 */

static CK_RV dump_attrs(CK_FUNCTION_LIST_PTR, CK_SESSION_HANDLE,
		  CK_OBJECT_HANDLE, CK_ULONG *, ...);

/*
 * Dump various flags
 */

struct flags {
    const char *name;
    CK_FLAGS value;
};

#define FV(name) { #name, name }
static struct flags slotflags[] = {
    FV(CKF_TOKEN_PRESENT),
    FV(CKF_REMOVABLE_DEVICE),
    FV(CKF_HW_SLOT),
    { NULL, 0 }
};
static struct flags tokenflags[] = {
    FV(CKF_RNG),
    FV(CKF_WRITE_PROTECTED),
    FV(CKF_LOGIN_REQUIRED),
    FV(CKF_USER_PIN_INITIALIZED),
    FV(CKF_RESTORE_KEY_NOT_NEEDED),
    FV(CKF_CLOCK_ON_TOKEN),
    FV(CKF_PROTECTED_AUTHENTICATION_PATH),
    FV(CKF_DUAL_CRYPTO_OPERATIONS),
    FV(CKF_TOKEN_INITIALIZED),
    FV(CKF_SECONDARY_AUTHENTICATION),
    FV(CKF_USER_PIN_COUNT_LOW),
    FV(CKF_USER_PIN_FINAL_TRY),
    FV(CKF_USER_PIN_LOCKED),
    FV(CKF_USER_PIN_TO_BE_CHANGED),
    FV(CKF_SO_PIN_COUNT_LOW),
    FV(CKF_SO_PIN_FINAL_TRY),
    FV(CKF_SO_PIN_LOCKED),
    FV(CKF_SO_PIN_TO_BE_CHANGED),
    { NULL, 0 }
};
static struct flags sessionflags[] = {
    FV(CKF_RW_SESSION),
    FV(CKF_SERIAL_SESSION),
    { NULL, 0 }
};
static struct flags mechflags[] = {
    FV(CKF_HW),
    FV(CKF_ENCRYPT),
    FV(CKF_DECRYPT),
    FV(CKF_DIGEST),
    FV(CKF_SIGN),
    FV(CKF_SIGN_RECOVER),
    FV(CKF_VERIFY),
    FV(CKF_VERIFY_RECOVER),
    FV(CKF_GENERATE),
    FV(CKF_GENERATE_KEY_PAIR),
    FV(CKF_WRAP),
    FV(CKF_UNWRAP),
    FV(CKF_DERIVE),
    FV(CKF_EXTENSION),
    { NULL, 0 }
};
#undef FV

static void flags_dump(struct flags *, CK_FLAGS);

/*
 * Our routines to output attribute information
 */

struct attr_handler {
    CK_ATTRIBUTE_TYPE attr;	/* Attribute */
    const char *label;	/* Attribute printed label */
    void (*dumper)(unsigned char *, unsigned int);	/* dumper function */
};

static void hexify_dump(unsigned char *, unsigned int);
static void string_dump(unsigned char *, unsigned int);
static void certtype_dump(unsigned char *, unsigned int);
static void length_dump(unsigned char *, unsigned int);
static void class_dump(unsigned char *, unsigned int);
static void mech_dump(unsigned char *, unsigned int);
static void mechlist_dump(unsigned char *, unsigned int);
static void keytype_dump(unsigned char *, unsigned int);

static struct attr_handler id_attr = {
    CKA_ID, "Key Identifier", hexify_dump
};

static struct attr_handler ctype_attr = {
    CKA_CERTIFICATE_TYPE, "Certificate Type", certtype_dump
};

static struct attr_handler value_attr = {
    CKA_VALUE, "Object value", length_dump
};

static struct attr_handler class_attr = {
    CKA_CLASS, "Object class", class_dump
};

static struct attr_handler app_attr = {
    CKA_APPLICATION, "Application Description", string_dump
};

static struct attr_handler objid_attr = {
    CKA_OBJECT_ID, "Object ID", hexify_dump
};

static struct attr_handler genmech_attr = {
    CKA_KEY_GEN_MECHANISM, "Key Generation Mechanism", mech_dump
};

static struct attr_handler allowedmech_attr = {
    CKA_ALLOWED_MECHANISMS, "Allowed Mechanisms", mechlist_dump
};

static struct attr_handler subject_attr = {
    CKA_SUBJECT, "Subject name", hexify_dump
};

static struct attr_handler keytype_attr = {
    CKA_KEY_TYPE, "Key type", keytype_dump
};

static struct attr_handler issuer_attr = {
    CKA_ISSUER, "Certificate issuer", hexify_dump
};

#if 0
static struct attr_handler value_attr = {
    CKA_VALUE, "Object value", hexify_dump
};
#endif

/*
 * A list of attribute information and associated filenames for output
 */

struct attr_list {
	CK_ATTRIBUTE_TYPE	attribute;
	CK_OBJECT_HANDLE	object;
	const char 		*filename;
	const char		*template;
	struct attr_list	*next;
};

#define LIBRARY_NAME ".libs/keychain-pkcs11.so"

/*
 * Read data into a buffer, reallocating it along the way
 */

static void getdata(const char *, unsigned char **, size_t *);
static CK_ULONG getnum(const char *, const char *);

static void
usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [flags] [library name]\n", progname);
    fprintf(stderr, "Library name defaults to: " LIBRARY_NAME "\n");
    fprintf(stderr, "Valid flags are:\n");
    fprintf(stderr, "\t-a attr\t\tNumeric attribute to dump (may be repeated "
    		    "with -F)\n");
    fprintf(stderr, "\t-c class\t\tNumeric class of objects to select; \n");
    fprintf(stderr, "\t\t\tdefault is to apply to all objects\n");
    fprintf(stderr, "\t-f file\t\tFile to dump attribute data to\n");
    fprintf(stderr, "\t-F template\tFilename template to dump file data;\n");
    fprintf(stderr, "\t\t\tfilename template supports the following items:\n");
    fprintf(stderr, "\t\t\t%%o\tObject number\n");
    fprintf(stderr, "\t\t\t%%a\tAttribute number\n");
    fprintf(stderr, "\t\t\t%%s\tSlot number\n");
    fprintf(stderr, "\t-L\t\tDo NOT log into card using C_Login\n");
    fprintf(stderr, "\t-N num\t\tSign <num> bytes of NULs (may be "
    		    "repeated)\n");
    fprintf(stderr, "\t-n progname\tSet program name to <progname>\n");
    fprintf(stderr, "\t-o object\tObject number to select for inspection or "
    		    "use for other\n");
    fprintf(stderr, "\t\t\toperations; affects next argument, may be repeated\n");
    fprintf(stderr, "\t-s slot\t\tSelect this slot (default: first slot);\n");
    fprintf(stderr, "\t\t\tmay be repeated\n");
    fprintf(stderr, "\t-S signdata\tData to sign; requires -o, "
		    "may be repeated\n");
    fprintf(stderr, "\t-T\t\tAllow the use of slots WITHOUT tokens\n");
    fprintf(stderr, "\t-v filename\tFilename of data to verify signature;\n");
    fprintf(stderr, "\t\t\tuse -V for signature data and -o to select key\n");
    fprintf(stderr, "\t-V filename\tSignature data for verification; use "
    	    "with -v and -o\n");
    fprintf(stderr, "\t-w\t\tInstead of exiting, wait for Control-C\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    CK_RV rv;
    CK_FUNCTION_LIST_PTR p11p;
    CK_SLOT_ID slot = -1;
    CK_SESSION_HANDLE hSession;
    /* CK_UTF8CHAR pin[64]; */
    /* CK_ULONG pinLen = sizeof(pin) - 1; */
    /* CK_BYTE label[32]; */

    CK_INFO info;
    CK_SLOT_INFO sInfo;
    CK_TOKEN_INFO tInfo;
    CK_SESSION_INFO sessionInfo;

    CK_OBJECT_HANDLE_PTR phObject;
    CK_ULONG maxSize;
    CK_ULONG count;

    CK_OBJECT_CLASS cls = -1;
    CK_CERTIFICATE_TYPE certtype;
    CK_ATTRIBUTE attrs[10];
    CK_ULONG ulValue;

    CK_ULONG numSlots = 0;
    CK_SLOT_ID_PTR slotList = NULL;
    CK_SLOT_INFO slotInfo;
    CK_OBJECT_HANDLE sObject = -1;
    CK_ATTRIBUTE_TYPE dumpType = -1;
    CK_MECHANISM_TYPE sMech = CKM_RSA_PKCS;
    CK_MECHANISM mech = { sMech, NULL, 0 };
    const char *verify_data = NULL;
    const char *verify_sig = NULL;
    const char *attr_filename = NULL;
    const char *attr_filetemplate = NULL;

    unsigned char *signbuf = NULL;
    unsigned int signsize = 0;
    bool sleepatexit = false;
    bool tokenlogin = true;
    bool requiretoken = true;

    struct attr_list *attr_head = NULL, *attr_tail = NULL, *attr;

    int i;

    while ((i = getopt(argc, argv, "a:c:f:F:LN:n:o:S:s:Tv:V:w")) != -1) {
	switch (i) {
	case 'a':
	    if (!attr_filename && !attr_filetemplate) {
		fprintf(stderr, "One of -f or -F must be given first!\n");
		exit(1);
	    }

	    attr = malloc(sizeof(struct attr_list));
	    attr->attribute = getnum(optarg, "Invalid attribute number");
	    attr->object = sObject;
	    attr->filename = attr_filename;
	    attr->template = attr_filetemplate;
	    attr->next = NULL;

	    if (!attr_tail) {
	    	attr_head = attr_tail = attr;
	    } else {
		attr_tail->next = attr;
		attr_tail = attr;
	    }

	    break;
	case 'f':
	    attr_filename = optarg;
	    attr_filetemplate = NULL;
	    break;
	case 'F':
	    attr_filetemplate = optarg;
	    attr_filename = NULL;
	    break;
	case 'L':
	    tokenlogin = false;
	    break;
	case 'N':
	    signsize = atoi(optarg);
	    signbuf = malloc(signsize);
	    memset(signbuf, 0, signsize);
	    break;
	case 'n':
#ifdef HAVE_SETPROGNAME
	    setprogname(optarg);
#endif /* HAVE_SETPROGNAME */
	    break;
	case 's':
	    slot = getnum(optarg, "Invalid slot number");
	    break;
	case 'S':
	    signbuf = (unsigned char *) optarg;
	    signsize = strlen(optarg);
	    break;
	case 'T':
	    requiretoken = false;
	    break;
	case 'o':
	    sObject = getnum(optarg, "Invalid object number");
	    break;
	case 'v':
	    verify_data = optarg;
	    break;
	case 'V':
	    verify_sig = optarg;
	    break;
	case 'w':
	    sleepatexit = true;
	    break;
	case '?':
	default:
	    usage(argv[0]);
	}
    }

    if ((verify_data || verify_sig) && (!verify_data || !verify_sig)) {
	fprintf(stderr, "Both -v and -V must be given\n");
	exit(1);
    }

    argc -= optind - 1;
    argv += optind - 1;

    if(argc == 1) {
        rv = load_library(LIBRARY_NAME, &p11p);
    } else {
        rv = load_library(argv[1], &p11p);
    }
    if (rv != CKR_OK) {
        fprintf(stderr, "Error loading library (rv = %X)\n", (unsigned int) rv);
        return(1);
    }

    rv = p11p->C_Initialize(NULL);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error initalizing library (rv = %X)\n", (unsigned int) rv);
        return(2);
    }

    memset(&info, 0, sizeof(info));
    rv = p11p->C_GetInfo(&info);
    if (rv == CKR_OK) {
        printf("PKCS#11 Version: %d.%d\n",info.cryptokiVersion.major, info.cryptokiVersion.minor);
        printf("Lib manufacturer: %s\n", stringify(info.manufacturerID, 32));
        printf("Lib description: %s\n", stringify(info.libraryDescription,32));
        printf("Lib version: %d.%d\n", info.libraryVersion.major, info.libraryVersion.minor);
        printf("Lib flags: %d\n", (int) info.flags);
    } else {
        fprintf(stderr, "Unable to get info (rv = %s)\n", getCKRName(rv));
    }

    rv = p11p->C_GetSlotList(requiretoken == true ? TRUE : FALSE, NULL,
			     &numSlots);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error getting Slot List\n");
        return(rv);
    }

    if (numSlots == 0) {
	fprintf(stderr, "No slots found!\n");
	exit(1);
    }

    printf("Found %d slots\n", (int) numSlots);

    slotList = (CK_SLOT_ID_PTR) malloc(sizeof(CK_SLOT_ID) * (numSlots + 1));

    rv = p11p->C_GetSlotList(requiretoken == true ? TRUE : FALSE, slotList,
			     &numSlots);
    if (rv != CKR_OK) {
        free(slotList);
        fprintf(stderr, "Error getting Slot List\n");
        return(rv);
    }

    for (i=0; i < numSlots; i++) {
        memset(&slotInfo, 0, sizeof(slotInfo));
	if (! p11p->C_GetSlotInfo) {
	    fprintf(stderr, "C_GetSlotInfo is NULL, continuing ...\n");
	    continue;
	}
        rv = p11p->C_GetSlotInfo(slotList[i], &slotInfo);
        if (rv != CKR_OK) continue;

        printf("Slot %d description: %s\n", (int) slotList[i],
	       stringify(slotInfo.slotDescription, 64));
    }

    if (! p11p->C_GetSlotInfo && numSlots > 0) {
	fprintf(stderr, "C_GetSlotInfo is NULL, assuming first slot "
		"is valid\n");
    }

    if (slotList) free(slotList);

    if (slot == -1)
	slot = slotList[0];

    memset(&sInfo, 0, sizeof(sInfo));
    if (p11p->C_GetSlotInfo) {
	rv = p11p->C_GetSlotInfo(slot, &sInfo);
    } else {
	rv = CKR_FUNCTION_NOT_SUPPORTED;
    }
    if (rv == CKR_OK) {
        printf("Slot Description: %s\n", stringify(sInfo.slotDescription, 64));
        printf("Slot Manufacturer: %s\n", stringify(sInfo.manufacturerID, 32));
        printf("Slot HW version: %d.%d\n", sInfo.hardwareVersion.major, sInfo.hardwareVersion.minor);
        printf("Slot FW version: %d.%d\n", sInfo.firmwareVersion.major, sInfo.firmwareVersion.minor);
	printf("Slot flags: ");
	flags_dump(slotflags, sInfo.flags);
	printf("\n");
    } else {
        fprintf(stderr, "Error getting slot info (rv = %s)\n", getCKRName(rv));
    }

    memset(&tInfo, 0, sizeof(tInfo));
    rv = p11p->C_GetTokenInfo(slot, &tInfo);
    if (rv == CKR_OK) {
        printf("Token label: %s\n", stringify(tInfo.label, 32));
        printf("Token Manufacturer: %s\n", stringify(tInfo.manufacturerID, 32));
        printf("Token Model: %s\n", stringify(tInfo.model,16));
        printf("Token Serial: %s\n", stringify(tInfo.serialNumber,16));
        printf("Token flags: ");
	flags_dump(tokenflags, tInfo.flags);
	printf("\n");
        printf("Token MaxSessionCount = %d\n", (int) tInfo.ulMaxSessionCount);
        printf("Token SessionCount = %d\n", (int) tInfo.ulSessionCount);
        printf("Token MaxRwSessionCount = %d\n", (int) tInfo.ulMaxRwSessionCount);
        printf("Token RwSessionCount = %d\n", (int) tInfo.ulRwSessionCount);
        printf("Token Max PIN len = %d\n", (int) tInfo.ulMaxPinLen);
        printf("Token Min PIN len = %d\n", (int) tInfo.ulMinPinLen);
        printf("Token total public mem = %d\n", (int) tInfo.ulTotalPublicMemory);
        printf("Token free public mem = %d\n", (int) tInfo.ulFreePublicMemory);
        printf("Token total private mem = %d\n", (int) tInfo.ulTotalPrivateMemory);
        printf("Token free private mem = %d\n", (int) tInfo.ulFreePrivateMemory);
        printf("Token hardware version = %d.%d\n", tInfo.hardwareVersion.major,
               tInfo.hardwareVersion.minor);
        printf("Token firmware version = %d.%d\n", tInfo.firmwareVersion.major,
               tInfo.firmwareVersion.minor);
        printf("Token utcTime = %s\n", stringify(tInfo.utcTime, 16));

    } else {
        fprintf(stderr, "Error getting token info (rv = %s)\n", getCKRName(rv));
    }

    if (p11p->C_GetMechanismList)
	rv = p11p->C_GetMechanismList(slot, NULL, &count);
    else
	rv = CKR_FUNCTION_NOT_SUPPORTED;

    if (rv == CKR_OK) {
	CK_MECHANISM_TYPE_PTR mechList = malloc(sizeof(*mechList) * count);
	rv = p11p->C_GetMechanismList(slot, mechList, &count);

	if (rv == CKR_OK) {
	    printf("Token supports %d mechanism%s\n", (int) count,
		   count > 0 ? "s" : "");
	    for (i = 0; i < count; i++) {
	    	CK_MECHANISM_INFO mechInfo;
		printf("%s\n", getCKMName(mechList[i]));
		if (p11p->C_GetMechanismInfo)
		    rv = p11p->C_GetMechanismInfo(slot, mechList[i], &mechInfo);
		else
		    rv = CKR_FUNCTION_NOT_SUPPORTED;
		if (rv != CKR_OK) {
		    fprintf(stderr, "C_GetMechanismInfo failed (rv = %s)\n",
			    getCKRName(rv));
		    break;
		}
		printf("Min key size = %lu, max key size = %lu\n",
		       mechInfo.ulMinKeySize, mechInfo.ulMaxKeySize);
		printf("Flags: ");
		flags_dump(mechflags, mechInfo.flags);
		printf("\n");
	    }
	}
	free(mechList);
    }

    if (rv != CKR_OK)
	fprintf(stderr, "GetMechanismList failed (rv = %s)\n", getCKRName(rv));

    rv = p11p->C_OpenSession(slot, CKF_SERIAL_SESSION, NULL, NULL, &hSession);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error opening session (rv = %s)\n", getCKRName(rv));
        goto cleanup;
    }

    memset(&sessionInfo, 0, sizeof(sessionInfo));
    if (p11p->C_GetSessionInfo)
	rv = p11p->C_GetSessionInfo(hSession, &sessionInfo);
    else
	rv = CKR_FUNCTION_NOT_SUPPORTED;

    if (rv == CKR_OK) {
        printf("Session slot: %d\n", (int) sessionInfo.slotID);
        printf("Session state: %d\n", (int) sessionInfo.state);
        printf("Session flags: %d\n", (int) sessionInfo.flags);
        printf("Session device errors: %d\n", (int) sessionInfo.ulDeviceError);
    } else {
        fprintf(stderr, "Unable to get session info (rv = %s)\n", getCKRName(rv));
    }

    if (tokenlogin) {
	rv = login(p11p, &tInfo, hSession, 0, NULL, 0);
	if (rv != CKR_OK) {
	    fprintf(stderr, "Error logging into token (rv = %s)\n",
		    getCKRName(rv));
	    (void)p11p->C_CloseSession(hSession);
	    goto cleanup;
	}
    }

    /*
     * If we are given an object, just extract that object's information.
     * If we are given an object class, then just find objects in that class.
     * Otherwise, find all objects.
     */

#if 0
    if (dumpType != -1) {
	FILE *out;
	attrs[0].type = dumpType;
	attrs[0].pValue = NULL;
	attrs[0].ulValueLen = 0;

	rv = p11p->C_GetAttributeValue(hSession, sObject, attrs, 1);

	if (rv != CKR_OK && rv != CKR_BUFFER_TOO_SMALL) {
	    fprintf(stderr, "C_GetAttributeValue dump for attribute %lu (%s) "
		    "failed (rv = %s)\n", dumpType, getCKAName(dumpType),
		    getCKRName(rv));
	    exit(1);
	}

	attrs[0].pValue = malloc(attrs[0].ulValueLen);

	rv = p11p->C_GetAttributeValue(hSession, sObject, attrs, 1);

	if (rv != CKR_OK) {
	    fprintf(stderr, "2nd call to C_GetAttributeValue failed "
		   "(rv = %s)\n", getCKRName(rv));
	    exit(1);
	}

	out = fopen(dumpFile, "w");

	if (! out) {
	    fprintf(stderr, "Unable to open \"%s\": %s\n", dumpFile,
		    strerror(errno));
	    exit(1);
	}

	printf("Writing %lu bytes to \"%s\" for attribute %lx (%s)\n",
	       attrs[0].ulValueLen, dumpFile, dumpType, getCKAName(dumpType));

	fwrite(attrs[0].pValue, attrs[0].ulValueLen, 1, out);

	fclose(out);
	free(attrs[0].pValue);
	exit(0);
    }




    rv = p11p->C_FindObjectsInit(hSession, NULL, 0);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error initializing Find Objects (rv = %s)\n", getCKRName(rv));
        (void)p11p->C_CloseSession(hSession);
        goto cleanup;
    }

    maxSize = 10;
    phObject = malloc(maxSize * sizeof(CK_OBJECT_HANDLE_PTR));


    do {
	rv = p11p->C_FindObjects(hSession, phObject, maxSize, &count);
	    if (rv != CKR_OK) {
		fprintf(stderr, "Error Finding Objects (rv = %s)\n",
			getCKRName(rv));
		(void)p11p->C_CloseSession(hSession);
		goto cleanup;
	    }

	fprintf(stderr, "Found %d objects\n", (int) count);

	for(i = 0; i < count; i++) {
	    printf("Object[%d] handle: %u\n", i, (unsigned int) phObject[i]);

	    rv = dump_attrs(p11p, hSession, phObject[i], &ulValue, &class_attr,
			    (void *) NULL);

	    if (rv != CKR_OK)
		continue;

	    switch(ulValue) {
            case CKO_DATA:
		dump_attrs(p11p, hSession, phObject[i], NULL, &app_attr,
			   &objid_attr, &value_attr, (void *) NULL);
                break;
            case CKO_CERTIFICATE:
		dump_attrs(p11p, hSession, phObject[i], NULL, &ctype_attr,
			   &id_attr, &value_attr, &subject_attr,
			   &issuer_attr, (void *) NULL);
                break;
	    case CKO_PUBLIC_KEY:
	    case CKO_PRIVATE_KEY:
		dump_attrs(p11p, hSession, phObject[i], NULL, &id_attr,
			   &keytype_attr, &genmech_attr, &allowedmech_attr,
			   &subject_attr, (void *) NULL);
		break;
	    }
	}
    } while (count > 0);

    rv = p11p->C_FindObjectsFinal(hSession);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error finalizing Finding Objects (rv = %X)\n", (unsigned int) rv);
        (void)p11p->C_CloseSession(hSession);
        goto cleanup;
    }


    cls = CKO_CERTIFICATE; //type CK_OBJECT_CLASS
    attrs[0].type = CKA_CLASS;
    attrs[0].pValue = &cls;
    attrs[0].ulValueLen = sizeof cls;

    certtype = CKC_X_509; //type CK_CERTIFICATE_TYPE
    attrs[1].type = CKA_CERTIFICATE_TYPE;
    attrs[1].pValue = &certtype;
    attrs[1].ulValueLen = sizeof certtype;


    rv = p11p->C_FindObjectsInit(hSession, attrs, 2);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error initializing Find Objects (rv = %X)\n", (unsigned int) rv);
        (void)p11p->C_CloseSession(hSession);
        goto cleanup;
    }

    do {
	rv = p11p->C_FindObjects(hSession, phObject, maxSize, &count);
	if (rv != CKR_OK) {
	    fprintf(stderr, "Error Finding Objects (rv = %X)\n", (unsigned int) rv);
	    (void)p11p->C_CloseSession(hSession);
	    goto cleanup;
	}

	fprintf(stderr, "Found %d objects\n", (int) count);

	for(i = 0; i < count; i++) {
	    rv = dump_attrs(p11p, hSession, phObject[i], &ulValue, &class_attr,
			    (void *) NULL);

	    if (rv != CKR_OK)
		continue;

	    switch(ulValue) {
            case CKO_CERTIFICATE:
		dump_attrs(p11p, hSession, phObject[i], NULL, &ctype_attr,
			   &id_attr, &value_attr, (void *) NULL);
                break;
	    }
	}
    } while (count > 0);

    rv = p11p->C_FindObjectsFinal(hSession);

    cls = CKO_PUBLIC_KEY;
    attrs[0].type = CKA_CLASS;
    attrs[0].pValue = &cls;
    attrs[0].ulValueLen = sizeof cls;

    rv = p11p->C_FindObjectsInit(hSession, attrs, 1);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error initializing Find Objects (rv = %X)\n", (unsigned int) rv);
        (void)p11p->C_CloseSession(hSession);
        goto cleanup;
    }

    do {
	rv = p11p->C_FindObjects(hSession, phObject, maxSize, &count);
	if (rv != CKR_OK) {
		fprintf(stderr, "Error Finding Objects (rv = %X)\n",
			(unsigned int) rv);
		(void)p11p->C_CloseSession(hSession);
		goto cleanup;
	}
	fprintf(stderr, "Found %d objects\n", (int) count);

	for(i = 0; i < count; i++) {
	    printf("Object[%d] handle: %u\n", i, (unsigned int) phObject[i]);
	    rv = dump_attrs(p11p, hSession, phObject[i], &ulValue, &class_attr,
			    &id_attr, (void *) NULL);
	}
    } while (count > 0);

    rv = p11p->C_FindObjectsFinal(hSession);

    cls = CKO_PRIVATE_KEY;
    attrs[0].type = CKA_CLASS;
    attrs[0].pValue = &cls;
    attrs[0].ulValueLen = sizeof cls;

    rv = p11p->C_FindObjectsInit(hSession, attrs, 1);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error initializing Find Objects (rv = %X)\n", (unsigned int) rv);
        (void)p11p->C_CloseSession(hSession);
        goto cleanup;
    }

    do {
	rv = p11p->C_FindObjects(hSession, phObject, maxSize, &count);
	if (rv != CKR_OK) {
		fprintf(stderr, "Error Finding Objects (rv = %X)\n",
			(unsigned int) rv);
		(void)p11p->C_CloseSession(hSession);
		goto cleanup;
	}
	fprintf(stderr, "Found %d objects\n", (int) count);

	for(i = 0; i < count; i++) {
	    printf("Object[%d] handle: %u\n", i, (unsigned int) phObject[i]);
	    if (sObject == -1)
		sObject = phObject[i];
	    rv = dump_attrs(p11p, hSession, phObject[i], &ulValue, &class_attr,
			    &id_attr, (void *) NULL);
	}
    } while (count > 0);

    rv = p11p->C_FindObjectsFinal(hSession);

    cls = CKA_VENDOR_DEFINED; //type CK_OBJECT_CLASS
    attrs[0].type = CKA_CLASS;
    attrs[0].pValue = &cls;
    attrs[0].ulValueLen = sizeof cls;

    rv = p11p->C_FindObjectsInit(hSession, attrs, 1);
    if (rv != CKR_OK) {
        fprintf(stderr, "Error initializing Find Objects (rv = %X)\n", (unsigned int) rv);
        (void)p11p->C_CloseSession(hSession);
        goto cleanup;
    }

    do {
	rv = p11p->C_FindObjects(hSession, phObject, maxSize, &count);
	if (rv != CKR_OK) {
		fprintf(stderr, "Error Finding Objects (rv = %X)\n",
			(unsigned int) rv);
		(void)p11p->C_CloseSession(hSession);
		goto cleanup;
	}
	fprintf(stderr, "Found %d objects\n", (int) count);

	for(i = 0; i < count; i++) {
	    printf("Object[%d] handle: %u\n", i, (unsigned int) phObject[i]);
	    rv = dump_attrs(p11p, hSession, phObject[i], &ulValue, &class_attr,
			    (void *) NULL);

	    if (rv != CKR_OK)
		continue;
	    switch(ulValue) {
            case CKO_DATA:
		dump_attrs(p11p, hSession, phObject[i], NULL, &app_attr,
			   &objid_attr, &value_attr, (void *) NULL);
		break;
	    }
        }

    } while (count > 0);

    rv = p11p->C_FindObjectsFinal(hSession);

    if (signbuf) {
	unsigned char data[1024];
	CK_ULONG datalen = sizeof(data);

	rv = p11p->C_SignInit(hSession, &mech, sObject);

	if (rv != CKR_OK) {
	    fprintf(stderr, "Error starting signature operation (rv = %s)\n",
		    getCKRName(rv));
	    exit(1);
	}

	rv = p11p->C_Sign(hSession, signbuf, signsize, data, &datalen);

	if (rv != CKR_OK) {
	    fprintf(stderr, "C_Sign failed (rv =  %s)\n", getCKRName(rv));
	    exit(1);
	}

	printf("Digest size = %lu, data = ", datalen);
	for (i = 0; i < datalen; i++)
	    printf("%02x", data[i]);
	printf("\n");

	/*
	 * Find the public key corresponding to this keyid, and try
	 * to verify it.
	 */

	if (p11p->C_VerifyInit) {
	    CK_ULONG vnum;
	    CK_OBJECT_HANDLE vObject;

	    attrs[0].type = CKA_ID;
	    attrs[0].pValue = NULL;
	    attrs[0].ulValueLen = 0;

	    rv = p11p->C_GetAttributeValue(hSession, sObject, attrs, 1);

	    if (rv != CKR_OK && rv != CKR_BUFFER_TOO_SMALL) {
		fprintf(stderr, "1st call to C_GettributeValue failed (%s)\n",
			getCKRName(rv));
		exit(1);
	    }

	    attrs[0].pValue = malloc(attrs[0].ulValueLen);

	    rv = p11p->C_GetAttributeValue(hSession, sObject, attrs, 1);

	    if (rv != CKR_OK) {
		fprintf(stderr, "2nd call to C_GettributeValue failed (%s)\n",
			getCKRName(rv));
		exit(1);
	    }

	    cls = CKO_PUBLIC_KEY;
	    attrs[1].type = CKA_CLASS;
	    attrs[1].pValue = &cls;
	    attrs[1].ulValueLen = sizeof cls;
	    rv = p11p->C_FindObjectsInit(hSession, attrs, 2);

	    if (rv != CKR_OK) {
		fprintf(stderr, "Call to C_FindObjectsInit failed (%s)\n",
			getCKRName(rv));
		exit(1);
	    }

	    rv = p11p->C_FindObjects(hSession, &vObject, 1, &vnum);

	    if (rv != CKR_OK) {
		fprintf(stderr, "Call to C_FindObjects failed (%s)\n",
			getCKRName(rv));
		exit(1);
	    }

	    if (vnum != 1) {
		fprintf(stderr, "No verify objects found\n");
		exit(1);
	    }

	    p11p->C_FindObjectsFinal(hSession);

	    free(attrs[0].pValue);

	    rv = p11p->C_VerifyInit(hSession, &mech, vObject);

	    if (rv != CKR_OK) {
		fprintf(stderr, "Call to C_VerifyInit failed (%s)\n",
			getCKRName(rv));
		exit(1);
	    }

	    rv = p11p->C_Verify(hSession, signbuf, signsize, data, datalen);

	    if (rv != CKR_OK) {
		fprintf(stderr, "Call to C_VerifyFailed failed (%s)\n",
			getCKRName(rv));
		exit(1);
	    } else {
		printf("signature was good!\n");
	    }
	}
    }

    if (verify_data) {
	unsigned char *d, *sig;
	size_t dlen, siglen;

	getdata(verify_data, &d, &dlen);
	getdata(verify_sig, &sig, &siglen);

	rv = p11p->C_VerifyInit(hSession, &mech, sObject);

	if (rv != CKR_OK) {
	    fprintf(stderr, "C_VerifyInit failed (rv = %s)\n", getCKRName(rv));
	    exit(1);
	}

	rv = p11p->C_Verify(hSession, d, dlen, sig, siglen);
	free(d);
	free(sig);

	if (rv != CKR_OK) {
	    fprintf(stderr, "C_Verify failed (rv = %s)\n", getCKRName(rv));
	    exit(1);
	} else {
	    printf("Good signature on %s/%s\n", verify_data, verify_sig);
	}
    }

#if 0

    rv = login(p11p, &tInfo, hSession, 0, NULL, 0);
     if (rv != CKR_OK) {
         fprintf(stderr, "Error logging into token (rv = %X)\n", (unsigned int) rv);
        (void)p11p->C_CloseSession(hSession);
        goto cleanup;
     }
#endif

    if (p11p->C_Logout)
	p11p->C_Logout(hSession);
    (void)p11p->C_CloseSession(hSession);
#endif
cleanup:
    if (p11p) p11p->C_Finalize(0);

    if (sleepatexit) {
	sigset_t set, oset;
	int sig;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigprocmask(SIG_BLOCK, &set, &oset);

	printf("Sleeping (pid %d) ... hit Control-C (INT) to exit ...",
	       getpid());
	fflush(stdout);

	if (sigwait(&set, &sig)) {
	    fprintf(stderr, "sigwait() failed: %s\n", strerror(errno));
	    exit(1);
	}

	sigprocmask(SIG_SETMASK, &oset, NULL);

	printf("done\n");
    }

    return 0;
}

CK_RV login(CK_FUNCTION_LIST_PTR p11p, CK_TOKEN_INFO_PTR tInfo, CK_SESSION_HANDLE hSession, int admin, CK_UTF8CHAR *password, CK_ULONG passwordLen) {
    CK_UTF8CHAR pin[64];
    CK_ULONG pinLen = sizeof(pin) - 1;
    CK_RV rv;

    if (tInfo->flags & CKF_PROTECTED_AUTHENTICATION_PATH) {
	printf("Protected authentication path found, not prompting PIN\n");
	return p11p->C_Login(hSession, admin == 1 ? CKU_SO : CKU_USER, NULL, 0);
    }

    if (passwordLen > 0 && password != NULL && passwordLen <= pinLen) {
        memcpy(pin, password, passwordLen);
        pinLen = passwordLen;
    } else {
        printf("Enter %sPIN: ", (admin == 1) ? "admin " : "");
        rv = getPassword(pin, &pinLen);
        if (rv!= 0)
            return(-1);
    }

    if (admin == 1)
        rv = p11p->C_Login(hSession, CKU_SO, pin, pinLen);
    else
        rv = p11p->C_Login(hSession, CKU_USER, pin, pinLen);

    memset(pin, 0, sizeof(pin));
    return(rv);
}


CK_RV load_library(char *library, CK_FUNCTION_LIST_PTR *p11p) {
    CK_RV rv;
    LpHandleType p11lib_handle = NULL;
    CK_RV (*getflist)(CK_FUNCTION_LIST_PTR_PTR);

    if (!library) {
        *p11p = NULL;
        return(-1);
    }
#ifdef _WIN32
    p11lib_handle = LoadLibrary(library);
#else
    p11lib_handle = dlopen(library, RTLD_NOW);
#endif
    if (p11lib_handle == NULL) {
#ifdef _WIN32
        printf("Error loading PKCS11 library: %s\n", (char *)GetLastError);
#else
        printf("Error loading PKCS11 library: %s\n", dlerror());
#endif
        return(EXIT_FAILURE);
    }

    getflist = (CK_RV (*)(CK_FUNCTION_LIST_PTR_PTR))
    GetFuncFromMod(p11lib_handle,
                   "C_GetFunctionList");
    if (getflist == NULL) {
#ifdef _WIN32
        printf("Error finding \"C_GetFunctionList\" symbol: %s\n",
               (char *)GetLastError);
#else
        printf("Error finding \"C_GetFunctionList\" symbol: %s\n", dlerror());
#endif
        return(EXIT_FAILURE);
    }

    rv = (*getflist)(p11p);
    if (rv != CKR_OK) {
        printf("Error calling \"C_GetFunctionList\" (rv = %s)\n",
	       getCKRName(rv));
        return(rv);
    }
    return(CKR_OK);
}

CK_RV getPassword(CK_UTF8CHAR *pass, CK_ULONG *length) {
#ifndef _WIN32
    struct termios t, save;
    int ret;
#else
    HANDLE handle;
    DWORD old_mode, new_mode;
#endif
    char *cp;

    if (pass == NULL || length == NULL)
        return(-1);

#ifndef _WIN32
    memset(&t, 0, sizeof(t));
    ret = tcgetattr(fileno(stdin), &t);
    if (ret) return(CKR_GENERAL_ERROR);

    save = t;
    t.c_lflag &= ~ECHO;

    ret = tcsetattr(fileno(stdin), TCSANOW, &t);
    if (ret) return(CKR_GENERAL_ERROR);
#else
    handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
        return ENOTTY;
    if (!GetConsoleMode(handle, &old_mode))
        return ENOTTY;

    new_mode = old_mode;
    new_mode |= ( ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT );
    new_mode &= ~( ENABLE_ECHO_INPUT );

    if (!SetConsoleMode(handle, new_mode))
        return ENOTTY;
    if (!SetConsoleMode(handle, old_mode))
        return ENOTTY;
    if (!SetConsoleMode(handle, new_mode))
        return ENOTTY;
#endif

    (void)fgets((char *)pass, (int)*length, stdin);
    cp = strchr((char *)pass, '\n');
    if (cp) *cp = (char)NULL;
    else pass[*length - 1] = (char)NULL;

    *length = (CK_ULONG)strlen((char *)pass);

#ifndef _WIN32
    ret = tcsetattr(fileno(stdin), TCSANOW, &save);
    if (ret) return(CKR_GENERAL_ERROR);
#else
    if (!SetConsoleMode(handle, old_mode))
        return ENOTTY;
#endif
    printf("\n");
    return(0);

}

/*
 * Dump out attributes for a specific object.  Argument list should end
 * with a NULL.
 */

static CK_RV
dump_attrs(CK_FUNCTION_LIST_PTR p11p, CK_SESSION_HANDLE session,
	   CK_OBJECT_HANDLE obj, CK_ULONG *retval, ...)
{
    va_list ap;
    struct attr_handler *ah;
    CK_ATTRIBUTE template;
    CK_RV rv, rvret = CKR_OK;
    bool valret = false;

    va_start(ap, retval);

    while ((ah = va_arg(ap, struct attr_handler *))) {
	template.type = ah->attr;
	template.pValue = NULL;
	template.ulValueLen = 0;
	rv = p11p->C_GetAttributeValue(session, obj, &template, 1);
	if (rv != CKR_OK) {
	    printf("%s: C_GetAttributeValue returned %s\n", ah->label,
		   getCKRName(rv));
	    rvret = rv;
	    continue;
	}
	if (template.ulValueLen == CK_UNAVAILABLE_INFORMATION) {
	    printf("%s: Information Unavailable\n", ah->label);
	    continue;
	}
	template.pValue = malloc(template.ulValueLen);
	rv = p11p->C_GetAttributeValue(session, obj, &template, 1);
	if (rv != CKR_OK) {
	    printf("%s: Second call to C_GetAttributeValue failed: %lu\n",
		   ah->label, rv);
	    free(template.pValue);
	    rvret = rv;
	    continue;
	}
	printf("%s: ", ah->label);
	(*ah->dumper)(template.pValue, template.ulValueLen);
	printf("\n");

	/*
	 * If we were passed in a value to return, then return the first
	 * item that was the correct size (sizeof(CK_ULONG))
	 */

	if (retval && !valret) {
	    *retval = *((CK_ULONG *) template.pValue);
	    valret = true;
	}

	free(template.pValue);
    }

    va_end(ap);

    return rvret;
}

/*
 * Dump attribute information, using hexify()
 */

static void
hexify_dump(unsigned char *data, unsigned int len)
{
    char *s = hexify(data, (int) len);

    printf("%s", s);
    free(s);
}

/*
 * Dump certificate type information
 */

static void
certtype_dump(unsigned char *data, unsigned int len)
{
    CK_CERTIFICATE_TYPE *type = (CK_CERTIFICATE_TYPE *) data;

    if (len != sizeof(CK_CERTIFICATE_TYPE)) {
    	printf("Unexpected length (got %d, expected %d)", (int) len,
	       (int) sizeof(CK_CERTIFICATE_TYPE));
	return;
    }

    switch (*type) {
    case CKC_X_509:
	printf("X.509 Certificate");
	break;
    case CKC_WTLS:
	printf("WTLS Certificate");
	break;
    case CKC_X_509_ATTR_CERT:
	printf("X.509 Attribute Certificate");
	break;
    default:
	printf("Unknown certificate type: %#lx", *type);
    }
}

/*
 * Dump class information
 */

static void
class_dump(unsigned char *data, unsigned int len)
{
    CK_OBJECT_CLASS *class = (CK_OBJECT_CLASS *) data;

    if (len != sizeof(CK_OBJECT_CLASS)) {
    	printf("Unexpected length (got %d, expected %d)", (int) len,
	       (int) sizeof(CK_OBJECT_CLASS));
	return;
    }

    printf("%s", getCKOName(*class));
}

/*
 * Just dump length
 */

static void 
length_dump(unsigned char *data, unsigned int len)
{
    printf("%u bytes", len);
}

/*
 * Dump this as a string
 */

static void
string_dump(unsigned char *data, unsigned int len)
{
    printf("%s", stringify(data, len));
}

/*
 * Dump this as a single mechanism
 */

static void
mech_dump(unsigned char *data, unsigned int len)
{
    CK_MECHANISM_TYPE *mech = (CK_MECHANISM_TYPE *) data;

    if (len != sizeof(CK_MECHANISM_TYPE)) {
    	printf("Unexpected length (got %d, expected %d)", (int) len,
	       (int) sizeof(CK_MECHANISM_TYPE));
	return;
    }

    printf("%s", getCKMName(*mech));
}

/*
 * Dump a list of mechanisms
 */

static void
mechlist_dump(unsigned char *data, unsigned int len)
{
    CK_MECHANISM_TYPE_PTR mechlist = (CK_MECHANISM_TYPE_PTR) data;
    unsigned int count = len / sizeof(CK_MECHANISM_TYPE);
    unsigned int i;

    for (i = 0; i < count; i++)
	printf("%s%s", i > 0 ? ", " : "", getCKMName(mechlist[i]));

}

/*
 * Dump a key type
 */

static void
keytype_dump(unsigned char *data, unsigned int len)
{
    CK_KEY_TYPE *keytype = (CK_KEY_TYPE *) data;

    if (len != sizeof(CK_KEY_TYPE)) {
    	printf("Unexpected length (got %d, expected %d)", (int) len,
	       (int) sizeof(CK_KEY_TYPE));
	return;
    }

    switch (*keytype) {
    case CKK_RSA:
    	printf("RSA Key");
	break;
    case CKK_DSA:
    	printf("DSA Key");
	break;
    default:
	printf("Unknown key type: %#lx", *keytype);
    }
}

/*
 * Dump a series of flags
 */

static void
flags_dump(struct flags *flagmap, CK_FLAGS flags)
{
    int i;
    bool hit = false;

    for (i = 0; flagmap[i].name != NULL; i++) {
	if (flags & flagmap[i].value) {
	    printf("%s%s", hit ? "|" : "", flagmap[i].name);
	    hit = true;
	}
    }
}

/*
 * Returns allocated data that needs to be free()d, exits on error.
 */

#define RSIZE 8192

static void
getdata(const char *filename, unsigned char **buf, size_t *size)
{
    int fd, cc;
    *size = 0;
    *buf = NULL;

    if ((fd = open(filename, O_RDONLY)) < 0) {
	fprintf(stderr, "Unable to open \"%s\": %s\n", filename,
		strerror(errno));
	exit(1);
    }

    do {
	*buf = realloc(*buf, *size + RSIZE);
	cc = read(fd, *buf + *size, RSIZE);

	if (cc < 0) {
	    fprintf(stderr, "Read on \"%s\" failed: %s\n", filename,
		    strerror(errno));
	    exit(1);
	}

	*size += cc;
    } while (cc > 0);

    close(fd);

    return;
}

/*
 * Returns a number, or exit()s on failure
 */

static CK_ULONG
getnum(const char *number, const char *errstring)
{
    char *endptr;
    unsigned long val;

    val = strtoul(number, &endptr, 0);

    if (*endptr != '\0') {
	fprintf(stderr, "%s: %s\n", errstring, number);
	exit(1);
    }

    return val;
}
