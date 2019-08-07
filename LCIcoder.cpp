/////////////////////////////////////////////////////////////////////////////////////////////

// LCIcoder.cpp

// (*) Encode (and decode) LCI (Location Configuration Information) strings, which contain:
//		(i) LCI element, (ii) Z subelement, (iii) Usage Rules/Policy subelement, (iv) colocated BSSIDS.
//		The LCI string is used in hostapd.conf to provide geodetic location information for the FTM RTT responder,
//		see 9.4.2.22.10 "Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) Specifications"
//		Its contents could have been revealed by RangingResult.getLci() in Android - but  that is blacklisted!
//		It does provide input data for Android's RangingResult.getResponderLocation() parser.

//		Location configuration information (LCI): As defined in IETF RFC 6225: 
//		includes latitude, longitude, and altitude, with uncertainty values for each. 
//		IETF RFC 6225: https://tools.ietf.org/html/rfc6225 

//	Based on the IEEE P802.11-REVmc/D8.0 spec section 9.4.2.22, under Measurement Report Element.
//	IEEE 8011-2016, RFC 6225, RFC 4776, and IETF RFC 3986 (with help from Roy Want).

#define  COPYRIGHT  \
	"Copyright (c) 2019 Berthold K.P. Horn <http://people.csail.mit.edu/bkph>." \
	"This source code is distributed under terms of the GNU General Public License," \
	"Version 3,  which grants certain rights to copy, modify, and redistribute." \
	"The license can be found at <http://www.gnu.org/licenses/>." \
	"There is no express or implied warranty, including merchantability or fitness" \
	"for a particular purpose." 

#define  VERSION   "Version 0.8.1"

////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: maxBSSIDindicator is not zero in current Android implementation - should be zero 

// TODO: ALTITUDE_ABOVE_GROUND=3 case missing in Android ResponderLocation class 

// See also https://issuetracker.google.com/135678941
// Location Information (LCR and LCI) missing in android.net.wifi.rtt.RangingResult

////////////////////////////////////////////////////////////////////////////////////////////////

// C/C++ code for MicroSoft Visual C++ 2017

// #include "pch.h"	// for MicroSoft Visual Studio 2017

#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1 
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT 1

// #define _CRT_SECURE_NO_WARNINGS 

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define INLINE __inline

//////////////////////////////////////////////////////////////////////////////////////////////

// The only valid version number for the LCI field (currently):
#define LCI_VERSION_1 1

// Latitude, longitude, altitude uncertainties are unsigned quantities (6 bit), 
// and values greater than 34 are reserved (IETF RFC 6225)	
// NOTE: *maximum* uncertainy codes corresponds to *minimum* uncertainty
#define MAX_LCI_UNCERTAINTY 34

// STA_Height_Above_Floor_Uncertainty is an unsigned quantity (8 bit)
// and values greater than 24 are reserved.
// NOTE: *maximum* uncertainy codes corresponds to *minimum* uncertainty
#define MAX_Z_UNCERTAINTY 24

#define MEASURE_TOKEN 1

#define MEASURE_REQUEST_MODE 0

// Type of Measurement Report
// NOTE: code here deals with Measurement Type LCI_TYPE (8) 

enum measurement_type {
//	BEACON_TYPE = 5,
//	FRAME_TYPE = 6,
	LCI_TYPE = 8,				// MEASURE_TYPE_LCI
	LOCATION_CIVIC_TYPE = 11,	// MEASURE_TYPE_LOCATION_CIVIC
//	LOCATION_IDENTIFIER_TYPE = 12,
//	FINE_TIME_MEASUREMENT_RANGE_TYPE = 16
};

// Subelement IDs for LCI report
// NOTE: code below deals with LCI_CODE, Z_CODE, USAGE_CODE and COLOCATED_BSSID subelements
// (because that is alll that Android API makes provision for)

enum lci_subelement_code {
	LCI_CODE = 0,
//	AZIMUTH_CODE = 1,
//	ORIGINATOR_MAC_ADDRESS = 2,
//	TARGET_MAC_ADDRESS = 3,
	Z_CODE = 4,
//	RELATIVE_ERROR_CODE = 5,
	USAGE_CODE = 6,
	COLOCATED_BSSID = 7,
//	VENDOR_SPECIFIC = 221
};

//	Note: Subelements are formatted exactly like elements 

//////////////////////////////////////////////////////////////////////////////////////////////

const char *copyright=COPYRIGHT;

const char *version=VERSION;

// Global flags controlling verbosity - set from command line

int verboseflag = 1;	// -v
int traceflag = 0;		// -t
int debugflag = 0;		// -d

int checkflag = 0;		// -c (check result after encoding or decoding)

//	Note: in the specification 0 uncertainty means uncertainty is *unknown*
//  Can treat 0 on command line instead as *smallest representable uncertainty*:

int	smallestflag = 0;	//  0 => zero uncertainty means *unknown* uncertainty code (default)
//	int	smallestflag = 1;	// 1 => zero uncertainty means *smallest* possible uncertainty code

int wantLCIflag = 1;		// encode LCI subelement 
int wantZflag = 1;			// encode Z subelement 
int wantUsageflag = 1;		// encode Usage Rules/Policy subelement 
int wantColocatedflag = 1;	// encode colocated BSSIDs (in LCI measurement element)

int sampleflag = 0;			// run an example of decoding and encoding an LCI string

///////////////////////////////////////////////////////////////////////////////

// Global variables used when encoding an LCI string - set from command line.
// Also, variables set by decoding a LCI string given on the command line (-lci=...)

// Needed for LCI subelement:

double latitude=0, longitude=0, altitude=0;
double latitude_uncertainty=0, longitude_uncertainty=0, altitude_uncertainty=0;

// Altitude_Type: 0 -> unknown, 1 -> meters, 2 -> floors, 3 -> height above ground in meters 
int Altitude_Type = 1;	// default

// Datum 1 -> WGS84, 2 -> NAD83_NAVD88, 3 -> NAD93_MLLWVD, otherwise unknown...
int datum = 1;		// default		

// The RegLoc Agreement field is set to 1 to report that the STA is operating within a 
// national policy area or an international agreement area near a national border; 
int RegLoc_Agreement = 0;

// The RegLoc DSE field is set to 1 to report that the enabling STA is enabling
// the operation of STAs with DSE (dynamic station enablement).
int RegLoc_DSE = 0;

// The Dependent STA field is set to 1 to report that the STA is operating with the
// enablement of the enabling STA whose LCI is being reported; 
int Dependent_STA = 0;

// The Version field is a 2-bit field defined in IETF RFC 6225.
int LCI_version = LCI_VERSION_1;	// the only value currently defined in IETF RFC 6225.

// Needed for Z subelement:

int expected_to_move = 0;	// 2 bits - must be zero for Android getResponderLocation()
double sta_floor = 0, sta_height_above_floor = 0, sta_height_above_floor_uncertainty = 0;

// Needed for Usage Rules/Policy subelement:

int	retransmission_allowed = 1;		// must be 1 for Android getResponderLocation()
int retention_expires_present = 0;	// must be 0 for Android getResponderLocation()
int STA_location_policy = 0;
int expiration = 0;		// expiration time (hours) (should be 0 unless retention_expires_present)

//////////////////////////////////////////////////////////////////////////////////////////////

char const * lcistring = NULL;		// lci string to decode if given on command line using -lci=...

int bssid_index = 0;				// points to next available slot in BSSIDS array

int max_bssids = 10;

char const **BSSIDS = NULL;			// array of strings of BSSIDs

// Each Address field contains a 48-bit address as defined in Clause 8 of IEEE Std 802-2014.

/////////////////////////////////////////////////////////////////////////////////////////////

// Utility functions

const char INLINE *strndup(const char *str, int nlen) {
	char *strnew = (char *) malloc(nlen+1);
//	strncpy(strnew, str, nlen);	// generic C version
	strncpy_s(strnew, nlen+1, str, nlen);	// Windows "safe" version
	strnew[nlen]='\0';	// null terminate
	return strnew;
}

int INLINE ishexdigit(int c) {
	if (c >= '0' && c <= '9') return 1;
	else if (c >= 'A' && c <= 'F') return 1;
	else if (c >= 'a' && c <= 'f') return 1;
	return 0;
}

int INLINE hextoint(int c) {	// hex character to integer
	if (c >= '0' && c <= '9') return (c & 0x0F);
	else if (c >= 'A' && c <= 'F') return (c & 0x0F) + 9;
	else if (c >= 'a' && c <= 'f') return (c & 0x0F) + 9;
	printf("ERROR in conversion from hexadecimal char to int: char %d\n", c);
	return 0;
}

int INLINE inttohex(int k) {	// integer to hex character (lc)
	if (k >= 0 && k <= 15) return "0123456789abcdef"[k]; 
	printf("ERROR in conversion from int to hexadecimal char: int %d\n", k);
	return 0;
}

void showbits(long long res, int nlen) {	// show MSB first - i.e. left to right
	long long mask = 1LL << (nlen - 1);
	for (int k = 0; k < nlen; k++, mask >>= 1)
		printf((res & mask) ? "1" : "0");
	printf("\n");
}

int INLINE getoctet(const char *str, int nbyt) { // big-endian
	return (hextoint(str[nbyt*2]) << 4) | hextoint(str[nbyt*2 + 1]);
}

int getnumber(const char *str, int nbyt, int ndig) { // get multibyte number
	int res = 0;
	for (int k = nbyt; k < nbyt + ndig; k++) 
		res = (res << 8) | getoctet(str, k);
	return res;
}

// read a bit --- LSB first - right to left within octet   (for LCI field)

int INLINE getbit(const char *str, int indx) {	// indx is in bits
	int nbyt = indx >> 3;			// 8 bits per octet
	int lft = indx - (nbyt << 3);	// bit position within octet
	int chr = getoctet(str, nbyt);
	return ((chr & (1 << lft)) != 0);
}

// read bits --- LSB first - right to left within octet   (for LCI field)

long long getbits(const char *str, int bstart, int nlen) {
	long long res = 0, bit = 1;
	for (int k = bstart; k < bstart + nlen; k++, bit <<= 1) {
		if (getbit(str, k)) res = res | bit;
	}
	if (debugflag) showbits(res, nlen);
	return res;
}

int INLINE putoctet(char *str, int nbyt, int oct) {	// big-endian
	str[nbyt*2] = (char)inttohex(oct >> 4);
	str[nbyt*2 + 1] = (char)inttohex(oct & 0x0F);
	return nbyt + 1;
}

int putnumber(char *str, int nbyt, int ndig, int num) {
	for (int k = nbyt; k < nbyt + ndig; k++) {
		int byt = (num >> (nbyt + ndig - k - 1) * 8) & 0xFF;
		putoctet(str, k, byt);
	}
	return nbyt + ndig;
}

// put bit --- LSB first - right to left within octet (for LCI field)

void INLINE putbit(char *str, int indx, int bit) {
	int nbyt = indx >> 3;		// 8 bits per octet
	int lft = indx - (nbyt << 3);
	int oct = getoctet(str, nbyt);
	int mask = (1 << lft);
	if (bit) oct |= mask;	// set that bit
	else oct &= ~mask;		// clear that bit
	putoctet(str, nbyt, oct);
}

// put bits --- LSB first - right to left within octet (for LCI field)

int putbits(char *str, int bstart, int nlen, long long val) {
	long long bit = 1LL;
	for (int k = bstart; k < bstart + nlen; k++, bit <<= 1) {
		putbit(str, k, ((val & bit) != 0));
	}
	return bstart + nlen;
}

int isValidBSSID (const char *str) {	// check MAC address format
	if (strlen(str) == 6*3-1) {			// 00:11:22:33:44:55  style
		for (int k=0; k < 6; k++) {	
			if (k > 0) {
				int sep = str[3*k-1];
				if (sep != ':' && sep != '-' && sep != '_') return 0;
			}
			if (! ishexdigit(str[3*k]) || ! ishexdigit(str[3*k+1])) return 0;
		}
		return 1;	// OK
	}
	else if (strlen(str) == 6*2) {		// 001122334455 style
		for (int k=0; k < 6; k++) {
			if (! ishexdigit(str[2*k]) || ! ishexdigit(str[2*k+1])) return 0;
		}
		return 1;	// OK
	}
	else return 0;	// bad format
}

// extract array of BSSID strings from comma-separated list on command line

void extractBSSID (const char *str) {
	const char *BSSID;
	while (*str != '\0') {
		if (bssid_index >= max_bssids) {
			max_bssids *= 2;
			BSSIDS = (const char **) realloc(BSSIDS, (max_bssids+1) * sizeof(const char *));
			if (BSSIDS == NULL) exit(1);
		}
		const char *strend = strchr(str, ',');
		if (strend == NULL) strend = str + strlen(str);
		BSSID = strndup(str, strend-str);
		if (isValidBSSID(BSSID)) BSSIDS[bssid_index++] = BSSID;
		else printf("ERROR: invalid colocated BSSID %s\n", BSSID);
		if (*strend == '\0') break;
		str = strend+1;
	}
}

// propagate sign bit from number with nlen bits to long long int

long long INLINE propagate_sign(long long res, int nlen) {
	if (traceflag) printf("propagate_sign %lld nlen %d\n", res, nlen);
	if (res & (1LL << (nlen - 1))) { // is sign bit (left most bit) on ?
		return (res | ~((1LL << nlen) - 1));
	}
	else return res;
}

// 2^{-n} with m bits after the binary dot 

double INLINE decodebinarydot(int n, int m) {	// 2^{m-n}
	return exp2(m-n);	// (Note: always positive)
}

// ...and the inverse: m - ceiling(log2(val))

int INLINE encodebinarydot(double val, int m) {
	double eps= 0.000001;	// to prevent coding/decoding disparity (round trip equality)
	if (val <= 0) {
		printf("ERROR: uncertainty %lg non-positive (while taking log2)\n", val);
		return 0;
	}
	if (debugflag) printf("val %10.9f log2(val) %10.9f ceil(log2(val)) %6.3f\n",
						  val, log2(val), ceil(log2(val)));
	int res = m - (int)ceil(log2(val)-eps);
	if (res <= 0) {
		printf("WARNING: uncertainty %lg way too large (i.e. resulting code non-positive)\n", val);
		return 1;	//  give smallest possible code (other than zero, which is code for unknown)
	}
	else if (res > MAX_LCI_UNCERTAINTY) {
		printf("WARNING: uncertainty %lg too small (i.e. resulting code too large %d > %d)\n",
			   val, res, MAX_LCI_UNCERTAINTY);
		return MAX_LCI_UNCERTAINTY;
	}
	else return res;
}

//////////////////////////////////////////////////////////////////////////////////

// Names for constant values here are from Android ResponderLocation class

enum datum_types {
	DATUM_UNDEFINED=0, DATUM_WGS84=1, DATUM_NAD83_NAV88=2, DATUM_NAD83_MLLW=3,
};

enum location_types {
	LOCATION_FIXED=0, LOCATION_VARIABLE=1, LOCATION_MOVEMENT_UNKNOWN=2, LOCATION_RESERVED=3
};

enum altitude_types {
	ALTITUDE_UNDEFINED=0, ALTITUDE_METERS=1, ALTITUDE_FLOORS=2,
	ALTITUDE_ABOVE_GROUND=3 // missing in Android ResponderLocation class ?
};

char *datum_string (int datums) {	// datum string
	switch(datums) {
		case DATUM_UNDEFINED: return "undefined";
		case DATUM_WGS84: return "WGS84";
		case DATUM_NAD83_NAV88: return "NAD83+ NAVD88 vertical reference";
		case DATUM_NAD83_MLLW: return "NAD83+ MLLWVD vertical reference"; // Mean Lower Low Water 
		default: return "unknown datum";
	}
}

char *altitude_type_string (int altitude_type) {	// altitude type string
	switch (altitude_type) {
		case ALTITUDE_UNDEFINED: return "undefined";
		case ALTITUDE_METERS: return "m";
		case ALTITUDE_FLOORS: return "floors";
		case ALTITUDE_ABOVE_GROUND: return "height above ground m";
		default: return "unknown altitude type";
	}
}

char *expected_to_move_string (int expected_to_moves) {	// expected_to_move string
	switch(expected_to_moves) {
		case LOCATION_FIXED: return "stationary"; // "fixed"
		case LOCATION_VARIABLE: return "expected to move";	// "variable"
		case LOCATION_MOVEMENT_UNKNOWN: return "movement pattern unknown";
		case LOCATION_RESERVED: return "reserved";
		default: return "unknown expected-to-move field value";
	}
}

// NOTE: Android will not pass any location data to users if the usage rights are set to:
//		1. Retransmission NOT allowed, or
//		2. Expiration after a period of time.

void checksettings (void) {
	if (!retransmission_allowed) 
		printf("WARNING: Android will not provide location information because retransmission_allowed is false\n");
	if (retention_expires_present)
		printf("WARNING: Android will not provide location information because retention_expires_present is true\n");
	if (expiration != 0)
		printf("WARNING: Android will not provide location information because expiration time != 0\n");
	if (expected_to_move)
		printf("WARNING: Android will not provide location information because expected_to_move is true\n");
}

/////////////////////////////////////////////////////////////////////////////////////////

// This encodes the LCI field (data part of the LCI element)

// This is based on the IEEE P802.11-REVmc/D8.0 spec section 9.4.2.22, under Measurement Report Element
// (as per detailed example on page 857)

// Note: LCI field sequence of conversions (two flips of bit order):
// binary, MSB first per field ->
// binary, LSB first per field ->
// binary, rearranged into octets
// binary, LSB first per octet ->
// binary, MSB first per octet

int encodeLCIfield(char *str, int nbyt) {

	putoctet(str, nbyt++, LCI_CODE);	// LCI subelement
	putoctet(str, nbyt++, 16);			// length

	int indx = nbyt << 3;	// bit index 
	if (verboseflag) printf("Encode LCI field ID %d (byte %d)\n", LCI_CODE, nbyt);

	// longitude and latitude as binary number with 25 bits after the dot
	long long Latitude  = (long long)roundl(latitude  * (1 << 25));
	long long Longitude = (long long)roundl(longitude * (1 << 25));
	// atltitude as binary number with 8 bits after the dot
	int Altitude = (int)round(altitude * (1 << 8));  

	int Latitude_Uncertainty, Longitude_Uncertainty, Altitude_Uncertainty;
	if (latitude_uncertainty > 0) Latitude_Uncertainty = encodebinarydot(latitude_uncertainty, 8);
	else if (! smallestflag) Latitude_Uncertainty = 0;	// treat as unknown (default)
	else Latitude_Uncertainty = MAX_LCI_UNCERTAINTY;	// (max in 6 bit field --- least uncertainty)
	if (longitude_uncertainty > 0) Longitude_Uncertainty = encodebinarydot(longitude_uncertainty, 8);
	else if (! smallestflag) Longitude_Uncertainty = 0;	// treat as unknown (default)
	else Longitude_Uncertainty = MAX_LCI_UNCERTAINTY;	// (max in 6 bit field --- least uncertainty)
	if (altitude_uncertainty > 0) Altitude_Uncertainty = encodebinarydot(altitude_uncertainty, 21);
	else if (! smallestflag) Altitude_Uncertainty = 0;	// treat as unknown (default)
	else Altitude_Uncertainty = MAX_LCI_UNCERTAINTY;	// (max in 6 bit field --- least uncertainty)
//	NOTE: actually, Altitude_Uncertainty applies only to Altitude_Type == 1 (?)

	if (verboseflag) {
		printf("Latitude %10.7f ->  %lld\n", latitude, Latitude);
		printf("Latitude_Uncertainty %lg -> %d\n", latitude_uncertainty, Latitude_Uncertainty);
		printf("Longitude %10.7f ->  %lld\n", longitude, Longitude);
		printf("Longitude_Uncertainty %lg ->  %d\n", longitude_uncertainty, Longitude_Uncertainty);
		char *Altitude_Type_String = altitude_type_string(Altitude_Type);
		printf("Altitude_Type %s -> %d\n", Altitude_Type_String, Altitude_Type);
		printf("Altitude %10.4f %s -> %d\n", altitude, Altitude_Type_String, Altitude);
		printf("Altitude_Uncertainty %lg %s ->  %d\n", altitude_uncertainty, Altitude_Type_String, Altitude_Uncertainty);
	}

	if (debugflag) printf("Starting LCI field coding\n");
	indx = putbits(str, indx, 6,  Latitude_Uncertainty);
	indx = putbits(str, indx, 34, Latitude);
	indx = putbits(str, indx, 6,  Longitude_Uncertainty);
	indx = putbits(str, indx, 34, Longitude);
	indx = putbits(str, indx, 4,  Altitude_Type);
	indx = putbits(str, indx, 6,  Altitude_Uncertainty);
	indx = putbits(str, indx, 30, Altitude);
	indx = putbits(str, indx, 3,  datum);
	indx = putbits(str, indx, 1,  RegLoc_Agreement);
	indx = putbits(str, indx, 1,  RegLoc_DSE);
	indx = putbits(str, indx, 1,  Dependent_STA);
	indx = putbits(str, indx, 2,  LCI_version);
	nbyt = indx >> 3;
	str[nbyt*2] = '\0';	// null terminate (assumes space available)
	if (debugflag) printf("indx %d byte %d\n", indx, nbyt);
	if (debugflag) printf("Ending LCI field coding\n");
	if (traceflag) printf("OUTPUT: %s\n", str);
	if (traceflag) printf("End of encodeLCIfield indx %d\n", indx);
	if (verboseflag) printf("\n");
	return nbyt;
}

int encodeZfield(char *str, int nbyt) {
	if (verboseflag) printf("Encode Z field ID %d (byte %d)\n", Z_CODE, nbyt);
	
	putoctet(str, nbyt++, Z_CODE);	// ID
	putoctet(str, nbyt++, 6);		// length
	
	int  STA_Floor_Info,  STA_Height_Above_Floor,  STA_Height_Above_Floor_Uncertainty;
	STA_Floor_Info = (expected_to_move & 0x03) | ((int)(sta_floor * 16.0)) << 2;
	STA_Height_Above_Floor = (int)(sta_height_above_floor * 4096.0);
	if (sta_height_above_floor_uncertainty > 0)
		STA_Height_Above_Floor_Uncertainty = encodebinarydot(sta_height_above_floor_uncertainty, 11);
	else if (! smallestflag) STA_Height_Above_Floor_Uncertainty = 0;	// implies height uncertainty unknown
	else STA_Height_Above_Floor_Uncertainty = MAX_Z_UNCERTAINTY;	// least uncertain
	if (STA_Height_Above_Floor_Uncertainty > MAX_Z_UNCERTAINTY)
		STA_Height_Above_Floor_Uncertainty = MAX_Z_UNCERTAINTY; // values 25 or higher are reserved
	if (verboseflag) {
		printf("expected_to_move %s -> %d\n", expected_to_move_string(expected_to_move), expected_to_move);
		printf("STA_Floor %lg -> %d\n", sta_floor, STA_Floor_Info & 0x6F);
		printf("STA_Height_Above_Floor %lg m -> %d\n", sta_height_above_floor, STA_Height_Above_Floor);
		printf("STA_Height_Above_Floor_Uncertainty %lg m -> %d\n",
			   sta_height_above_floor_uncertainty,	STA_Height_Above_Floor_Uncertainty);
	}
	putnumber(str, nbyt, 2, STA_Floor_Info);
	nbyt += 2;
	putnumber(str, nbyt, 3, STA_Height_Above_Floor);
	nbyt += 3;
	putnumber(str, nbyt, 1, STA_Height_Above_Floor_Uncertainty);
	nbyt += 1;
	str[nbyt*2] = '\0';	// null terminate (assumes space available)
	if (traceflag) printf("encodeZfield byte %d str %s\n", nbyt, str);
	if (verboseflag) printf("\n");
	return nbyt;
}

int encodeUsageField(char *str, int nbyt) {
	if (verboseflag) printf("Encode Usage Field ID %d (byte %d)\n", USAGE_CODE, nbyt);
	int nlen = retention_expires_present ? 3 : 1; // default length of expiration field 
	if (retention_expires_present) {
		if (expiration == 0) {
			printf("WARNING: Inconsistency: Retention_expires_present true but expiration == 0\n");
			retention_expires_present = false;	// override
			nlen = 1;
		}
	}
	else {
		if (expiration != 0) {
			printf("WARNING: Inconsistency: Retention_expires_present false but expiration != 0\n");
			retention_expires_present = true;	// override
			nlen = 3;
		}
	}

	putoctet(str, nbyt++, USAGE_CODE);	// ID
	putoctet(str, nbyt++, nlen);		// length
	
	int parameters = retransmission_allowed | (retention_expires_present << 1) | (STA_location_policy << 2);
	if (verboseflag) {
		printf("Retransmission_Allowed %s -> %d\n",
			   retransmission_allowed ? "true":"false", retransmission_allowed);
		printf("Retention_Expires_Relative_Present %s -> %d\n",
			   retention_expires_present ? "true":"false", retention_expires_present);
		printf("STA_Location_Policy %s -> %d\n",
			   STA_location_policy ? "true":"false", STA_location_policy);
	}
	putoctet(str, nbyt++, parameters);
	if (retention_expires_present) {
		putnumber(str, nbyt, 2, expiration);
		nbyt += 2;
	}
	if (traceflag) printf("encodeUsageField byte %d\n", nbyt);
	str[nbyt*2] = '\0';	// null terminate (assumes space available)
	if (verboseflag) printf("\n");
	return nbyt;
}

void printBSSID(const char *str) {
	for (int k = 0; k < 6; k++) {
		printf("%c%c%c", str[k*2], str[k*2+1], (k < 5) ? ':' : '\0');
	}
}

void showColocatedBSSIDs () {
	printf("Colocated BSSIDs:\n");
	for (int k = 0; k < bssid_index; k++) {
		printf("%d\t", k);
		printBSSID(BSSIDS[k]);
		printf("\n");
	}
}

int placeBSSID (char *str, int nbyt, const char *bssid) {
	if (strlen(bssid) == 6*3-1) {	// 11:22:33:44:55:66 format
		for (int k = 0; k < 6; k++, nbyt++) {
			str[nbyt*2] = bssid[k*3];
			str[nbyt*2+1] = bssid[k*3+1];
		}
	}
	else if (strlen(bssid) == 6*2) {	// 112233445566 format
		for (int k = 0; k < 6; k++, nbyt++) {
			str[nbyt*2] = bssid[k*2];
			str[nbyt*2+1] = bssid[k*2+1];
		}
	}
	else printf("ERROR: invalid BSSID format %s\n", bssid);
	return nbyt;
}

int encodeColocatedBSSID(char *str, int nbyt, int nbssids) {
	if (bssid_index == 0) return nbyt;	// nothing to do
//	int maxBSSIDindicator = 0;	// official value (9.4.2.22.10 Fig.	9-224)
	int maxBSSIDindicator = bssid_index;	// current Android implementation
	putoctet(str, nbyt++, COLOCATED_BSSID); // ID
	putoctet(str, nbyt++, nbssids*6 + 1);	// length
	putoctet(str, nbyt++, maxBSSIDindicator);	// should really be 0...
	for (int k=0; k < bssid_index; k++) 
		nbyt = placeBSSID(str, nbyt, BSSIDS[k]);
	str[nbyt*2]='\0';	// zero terminate (assumes space available)
	return nbyt;
}

int decodeColocatedBSSID(const char *str, int nbyt, int nlen) {	// str points past ID and length octets
	int maxBSSIDindicator = getoctet(str, nbyt++);
	if (maxBSSIDindicator != 0) {
		printf("WARNING: maxBSSIDindicator %d != 0\n",
			   maxBSSIDindicator);	// official value (9.4.2.22.10 Fig.	9-224)
		if (maxBSSIDindicator != (nlen-1)/6)
			printf("WARNING: maxBSSIDindicator %d != %d\n",
				   maxBSSIDindicator, (nlen-1)/6);	// current Android implementation
	}
	// Note: base the number of BSSIDs on length of field, not maxBSSIDindicator,
	// since maxBSSIDindicator is *supposed* to be zero
	int nBSSID = (nlen-1)/6;
	for (int k = 0; k < nBSSID; k++) {
		BSSIDS[k] = strndup(str+nbyt*2, 6*2);
		nbyt += 6;
		if (! isValidBSSID(BSSIDS[k])) printf("ERROR: invalid BSSID %s\n", BSSIDS[k]);
	}
	bssid_index = nBSSID;	// k
	return nbyt;
}

// The Co-Located BSSID list subelement is used to report the list of BSSIDs 
// of the BSSs that share the same antenna connector with the reporting STA.
// (i.e., it is not really a list of neighboring BSSIDs...)
// TODO: Can this appear as subelement of *both* LCI and of CIVIC elements ?

////////////////////////////////////////////////////////////////////////////////////////////////////

// This decodes the LCI field (data part of the LCI element)

// NOTE: LCI field sequence of conversions (two flips of bit order):
// binary, MSB first per field ->
// binary, LSB first per field ->
// binary, rearranged into octets
// binary, LSB first per octet ->
// binary, MSB first per octet

// writes values directly into global variables...

int decodeLCIfield(const char *str, int indx) {

	if (traceflag) printf("decodeLCIfield indx %d (byte %d)\n", indx, indx >> 3);
	if (debugflag) printf("Input: %s\n", str);

	int Latitude_Uncertainty = (int)getbits(str, indx, 6);
	indx += 6;	// advance 6 bits
	if (Latitude_Uncertainty > MAX_LCI_UNCERTAINTY) {
		printf("ERROR: latitude uncertainty code %d > %d\n", Latitude_Uncertainty, MAX_LCI_UNCERTAINTY);
		Latitude_Uncertainty = MAX_LCI_UNCERTAINTY;
	}
	// latitude uncertainty code zero means "unknown"
	if (Latitude_Uncertainty == 0) latitude_uncertainty = 0;
	else latitude_uncertainty = decodebinarydot(Latitude_Uncertainty, 8);

	long long Latitude = getbits(str, indx, 34);
	indx += 34;	// advance 34 bits
	Latitude = propagate_sign(Latitude, 34);
	latitude = Latitude / (double)(1 << 25);

	int Longitude_Uncertainty = (int)getbits(str, indx, 6);
	indx += 6;
	if (Longitude_Uncertainty > MAX_LCI_UNCERTAINTY) {
		printf("ERROR: longitude uncertainty code %d> %d\n", Longitude_Uncertainty, MAX_LCI_UNCERTAINTY);
		Longitude_Uncertainty = MAX_LCI_UNCERTAINTY;
	}
	// longitude uncertainty code zero means "unknown"
	if (Longitude_Uncertainty == 0) longitude_uncertainty = 0;
	else longitude_uncertainty = decodebinarydot(Longitude_Uncertainty, 8);

	long long Longitude = getbits(str, indx, 34);
	indx += 34;
	Longitude = propagate_sign(Longitude, 34);
	longitude = Longitude / (double)(1 << 25);

	Altitude_Type = (int) getbits(str, indx, 4);
	indx += 4;

	int Altitude_Uncertainty = (int)getbits(str, indx, 6);
	indx += 6;
	if (Altitude_Uncertainty > MAX_LCI_UNCERTAINTY) {
		printf("ERROR: Altitude uncertainty code %d > %d\n", Altitude_Uncertainty, MAX_LCI_UNCERTAINTY);
		Altitude_Uncertainty = MAX_LCI_UNCERTAINTY;
	}
	// altitude uncertainty code zero means unknown
	if (Altitude_Uncertainty == 0) altitude_uncertainty = 0;
	else altitude_uncertainty = decodebinarydot(Altitude_Uncertainty, 21);
//	NOTE: actually, Altitude_Uncertainty only applies to Altitude_Type == 1

	int Altitude = (int) getbits(str, indx, 30);
	indx += 30;
	altitude = Altitude / 256.0; // coded as 8-bit fraction

	if (verboseflag) {
		printf("Latitude %lld ->  %10.7f\n", Latitude, latitude);
		printf("Latitude_Uncertainty %d -> %lg degrees\n", Latitude_Uncertainty, latitude_uncertainty);
		printf("Longitude %lld ->  %10.7f\n", Longitude, longitude);
		printf("Longitude_Uncertainty %d -> %lg degrees\n", Longitude_Uncertainty, longitude_uncertainty);
		char *Altitude_Type_String = altitude_type_string(Altitude_Type);
		printf("Altitude_Type %d -> %s\n", Altitude_Type, Altitude_Type_String);
		printf("Altitude %d ->  %10.4f %s\n", Altitude, altitude, Altitude_Type_String);
		printf("Altitude_Uncertainty %d -> %lg %s\n", Altitude_Uncertainty, altitude_uncertainty, Altitude_Type_String);
	}

	datum = (int)getbits(str, indx, 3);
	indx += 3;
	RegLoc_Agreement = (int)getbits(str, indx, 1);
	indx += 1;
	RegLoc_DSE = (int)getbits(str, indx, 1);
	indx += 1;
	Dependent_STA = (int)getbits(str, indx, 1);
	indx += 1;
	LCI_version = (int)getbits(str, indx, 2);
	indx += 2;
	if (LCI_version != LCI_VERSION_1)
		printf("ERROR: LCI Version %d is not %d\n", LCI_version, LCI_VERSION_1);

	if (traceflag) {	
		printf("Datum %d -> %s\n", datum, datum_string(datum));
//		following wouldn't normally be different from the defaults...
		printf("RegLoc_Agreement %d\n", RegLoc_Agreement);
		printf("RegLoc_DSE %d\n", RegLoc_DSE);
		printf("Dependent_STA %d\n", Dependent_STA);
		printf("LCI Version %d\n", LCI_version);
	}

	if (debugflag) printf("End of decodeLCIField indx %d (%d bytes)\n", indx, indx >> 3);
	if (traceflag) printf("\n");
	return indx;
}

/////////////////////////////////////////////////////////////////////////////////////

// Decodes hexadecimal LCI string used in lci="..." in hostapd.config

void decodeLCIstring (const char *str) {
	int STA_Floor_Info, STA_Height_Above_Floor, STA_Height_Above_Floor_Uncertainty;
	int parameters;
	int nbyt = 0;
	int slen = strlen(str) / 2;	// how many bytes represented by hex string
	if (traceflag) printf("slen %d str %s\n", slen, str);
	int a = getoctet(str, nbyt++);	// 01 MEASUREMENT_REPORT ?
	int b = getoctet(str, nbyt++);	// 00
	int c = getoctet(str, nbyt++);	// 08 (LCI_TYPE) (Measurement Type Table 9-107)
	if (debugflag) printf("%0x %0x %0x byte %d\n", a, b, c, nbyt);
	if (a != MEASURE_TOKEN || b != MEASURE_REQUEST_MODE || c != LCI_TYPE)
		printf("ERROR: Bad Measurement Element Type %0x %0x %0x\n", a, b, c);
	
//	Now look for the subelements and parse them
	while (nbyt < slen && str[nbyt*2] != '\0') {
		int indx;
		int ID = getoctet(str, nbyt++);		// subelement ID
		int nlen = getoctet(str, nbyt++);	// subelement field length
		if (traceflag) printf("ID %d nlen %d byte %d (slen %d)\n", ID, nlen, nbyt, slen);
		if (nbyt + nlen > slen) {	// don't try and parse past end of string
			printf("ERROR: bad length code ID %d nlen %d (nbyt %d slen %d)\n", ID, nlen, nbyt, slen);
			break;
		}
		switch (ID) {
			
		//	The LCI Subelement field contains an LCI subelement.
		//	The LCI subelement is formatted as shown in	Figure 9-214.
		//	The (optional) LCI field is formatted as shown in Figure 9-215.
		case LCI_CODE:	// LCI
			if (verboseflag) printf("LCI subelement: ID %d length %d (byte %d)\n", ID, nlen, nbyt);
			if (nlen == 0) break;	// nothing to do
			if (nlen != 16) {
				printf("ERROR: Unexpected length %d for LCI element\n", nlen);
				nbyt += nlen;
				break;	// don't even try to decode it...
			}
			indx = decodeLCIfield(str+nbyt*2, 0);
			if (indx != 128) printf("ERROR: length of LCI subelement wrong %d bits (should be 128 bits)\n", indx);
			nbyt += indx >> 3;	// advance 16 bytes
			if (debugflag) printf("DecodeLCIstring bit indx %d byte %d (slen %d)\n", indx, nbyt, slen);
			if (verboseflag) printf("\n");
			break;

		//	The Z subelement is used to report the floor and location of the STA with respect to the floor level. 
		//	The format of the Z subelement is shown in Figure 9-218.
		//	The format of the STA Floor Info field is defined in Figure	9-219.
		case Z_CODE:
			if (verboseflag) printf("Z subelement: ID %d length %d (byte %d)\n", ID, nlen, nbyt);
			if (nlen != 6) 	printf("ERROR: Unexpected length %d for Z subelement\n", nlen);
//			if (nlen != 6) { 
			if (nlen != 6 && nlen != 5) { 	// allow for buggy Z subelements ?
				nbyt += nlen;
				break;	// don't even try to decode it...
			}
			STA_Floor_Info = getnumber(str, nbyt, 2);
			nbyt += 2;
			expected_to_move = STA_Floor_Info & 0x03;		// two LSB bits
			sta_floor = (double)(STA_Floor_Info >> 2) / 16.0;	// 14 MSB bits - units of 1/16 floors
			// The following have not been dealt with explicitly here
			// -8192 => unknown STA floor
			// -8191 => STA -8191/16 floors or less
			//  8191 => STA  8191/16 floors or more
			// Allow for incorrect length of Z element:
			if (nlen == 5) {
				STA_Height_Above_Floor = getnumber(str, nbyt, 2);	// wrong
				nbyt += 2;
			}
			else {
				STA_Height_Above_Floor = getnumber(str, nbyt, 3);	// correct
				nbyt += 3;
			}
			// The following have not been dealt with explicitly  here
			// 8 388 608 => unknown STA height above floor
			// 8 388 607 => 8 388 607/4096 m or less
			//  8 388 607 =>  8 388 607/4096 m or more
			sta_height_above_floor = (double)STA_Height_Above_Floor / 4096.0;
//			STA_Height_Above_Floor_Uncertainty = getnumber(str, nbyt++, 1);
			STA_Height_Above_Floor_Uncertainty = getoctet(str, nbyt++);
			// NOTE: 0 here means height above floor uncertainty unknown 
			if (STA_Height_Above_Floor_Uncertainty > MAX_Z_UNCERTAINTY)
					printf("ERROR: STA_Height_Above_Floor_Uncertainty %d > %d\n",
						  STA_Height_Above_Floor_Uncertainty, MAX_Z_UNCERTAINTY);
			if (STA_Height_Above_Floor_Uncertainty > 0)
				sta_height_above_floor_uncertainty = decodebinarydot(STA_Height_Above_Floor_Uncertainty, 11);
			else sta_height_above_floor_uncertainty = 0;	// code for height uncertainty unknown 
			if (verboseflag) {
				printf("expected_to_move %d -> %s\n", expected_to_move, expected_to_move_string(expected_to_move));
				printf("STA_Floor %d -> %lg\n", (STA_Floor_Info & 0x6F), sta_floor);
				printf("STA_Height_Above_Floor %d -> %lg m\n",
					   STA_Height_Above_Floor, sta_height_above_floor);
				printf("STA_Height_Above_Floor_Uncertainty %d -> %lg m\n", 
					   STA_Height_Above_Floor_Uncertainty, sta_height_above_floor_uncertainty);
				printf("\n");
			}
			break;

		// The Usage Rules/Policy subelement is used to report the usage rules of the reporting STA and whether
		//	additional STA or neighboring STA location information is available if the additional information can be
		//	transferred more securely. The format of the Usage Rules/Policy subelement is defined in Figure 9-222.
		case USAGE_CODE:
			if (verboseflag) printf("Usage Rules/Policy subelement: ID %d length %d (byte %d)\n", ID, nlen, nbyt);
			if (nlen != 1 && nlen != 3) {
				printf("ERROR: Unexpected length %d for Usage Rules/Policy subelement\n", nlen);
				nbyt += nlen;
				break;	// don't even try to decode it...
			}
			parameters = getoctet(str, nbyt++);
			retransmission_allowed = ((parameters & 1) != 0);
			retention_expires_present = ((parameters & 2) != 0);
			STA_location_policy = ((parameters & 4) != 0);
			if (verboseflag) {
				printf("Retransmission_Allowed %d -> %s\n",
					   retransmission_allowed, retransmission_allowed ? "true":"false");
				printf("Retention_Expires_Relative_Present %d -> %s\n",
					   retention_expires_present, retention_expires_present ? "true":"false");
				printf("STA_Location_Policy %d -> %s\n",
					   STA_location_policy, STA_location_policy ? "true":"false");
			}
			if (nlen == 1) expiration = 0;
			else if (nlen == 3) {
				expiration = getnumber(str, nbyt, 2);
				if (verboseflag) printf("Expiration %d hours\n", expiration);
 //				WARNING: Android will not provide location information if expiration != 0
				nbyt += nlen - 1;
			}
//			else printf("ERROR: length of Usage field %d octets (not 1 or 3)\n", nlen); 
			if (retention_expires_present && nlen != 3)
				printf("WARNING: Inconsistent fields: retention_expires_present true with nlen %d != 3\n", nlen);
//			if (!retention_expires_present && nlen != 1)
			if (!retention_expires_present && nlen != 1 && expiration != 0)
				printf("WARNING: Inconsistent fields: retention_expires_present false with nlen %d != 1\n", nlen);
			// NOTE: If the Usage rights subelement (06) does not have an expiration bit set, 
			// then there should be no expiration time field. 
			// NOTE:the above ignores the common error of nlen == 3 and expiration == 0
			// (The Usage Rights subelement should therefore be simply 06 01 01)
			if (verboseflag) printf("\n");
			break;

		case COLOCATED_BSSID:
			if (verboseflag) printf("Colocated BSSIDS subelement: ID %d length %d (byte %d)\n", ID, nlen, nbyt);
			if ((nlen-1) % 6 != 0) printf("ERROR: length %d\n", nlen);
			nbyt = decodeColocatedBSSID(str, nbyt, nlen);
			if (bssid_index > 0) showColocatedBSSIDs();
			if (traceflag) printf("bssid_index %d nbyt %d \n", bssid_index, nbyt);
			if (verboseflag) printf("\n");
			break;
			
		default:
			printf("ERROR: Unrecognized subelement: ID %d length %d at octet %d\n", ID, nlen, nbyt-2);
			nbyt += nlen;
			break;
		}
		if (traceflag) printf("\n");
	}
	checksettings();
	if (debugflag) printf("End of decoding LCI string byte %d slen %d\n", nbyt, slen);
	if (debugflag) printf("\n");
}

char *encodeLCIstring (void) {
	int nbyt = 3;		// space for Measurement Report header 
	nbyt += (2 + 16);	// space for LCI subelement
	nbyt += (2 + 6);	// space for Usage subelement
	nbyt += (2 + 3);	// space for Z subelement
	nbyt += (2 + 6 * bssid_index + 1);	// space for colocated BSSID subelement 
	int nlen = nbyt * 2;	// number of hexadecimal characters in string
	if (debugflag) printf("Allocating %d bytes\n", nlen+1);
	char *str = (char *) malloc(nlen+1);
	if (str == NULL) exit(1);
	memset(str, '0', nlen);
	str[nlen] = '\0';
	nbyt = 0;
	checksettings();
//	Measurement Report Type header first
	putoctet(str, nbyt++, MEASURE_TOKEN);			// 1
	putoctet(str, nbyt++, MEASURE_REQUEST_MODE);	// 0
	putoctet(str, nbyt++, LCI_TYPE);				// 08 (LCI_TYPE) (Measurement Type Table 9-107)
	if (debugflag) printf("After header byte %d\n", nbyt);
//	Subelements within an element are ordered by nondecreasing Subelement ID. See 10.27.9.
	int needLCIflag = (latitude != 0 || longitude != 0 || altitude != 0);
//	if (wantLCIflag && needLCIflag) {
	if (wantLCIflag) {
		nbyt = encodeLCIfield(str, nbyt);
		if (traceflag) printf("str %s byte %d\n", str, nbyt);
	}
	int needZflag = (sta_floor != 0 || sta_height_above_floor != 0 || sta_height_above_floor_uncertainty != 0);
//	if (wantZflag && needZflag) {
	if (wantZflag) {
		nbyt = encodeZfield(str, nbyt);
		if (traceflag) printf("str %s byte %d\n", str, nbyt);
	}
	int needBSSIDflag = (bssid_index > 0);
//	if (wantColocatedflag) {
	if (wantColocatedflag && needBSSIDflag) {
		nbyt = encodeColocatedBSSID(str, nbyt, bssid_index);
		if (traceflag) printf("str %s byte %d\n", str, nbyt);		
	}
	int needUsageFlag = (needLCIflag || needZflag || needBSSIDflag);
//	if (wantUsageflag) {
	if (wantUsageflag && needUsageFlag) {
		nbyt = encodeUsageField(str, nbyt);
		if (traceflag) printf("str %s byte %d\n", str, nbyt);
	}
	return str;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

// Test code - buggy examples originally from hostapd.conf

// const char *lci1 = "010008001052834d12efd2b08b9b4bf1cc2c000041060300000004050000000012";	// original (broken)

// from hostapd testgas.py

// const char *lci1a = "010008001052834d12efd2b08b9b4bf1cc2c00004104050000000000060100" // Sydney Opera House bad

const char *lci2 = "010008001052834d12efd2b08b9b4bf1cc2c0000410406000000000012060101";	// Sydney Opera House fixed

// const char *lci2a = "010008001052834d12efd2b08b9b4bf1cc2c00004106030100000406000000000012"; // bad: 0603010000

// another buggy example, from https://w1.fi/cgit/hostap/plain/tests/hwsim/test_rrm.py

const char *lci3 = "01000800101298c0b512926666f6c2f1001c00004104050000c00012";	// broken

///////////////////////////////////////////////////////////////////////////////

// bits of test code

void testbinarydot (int nmax) {	// test binary log2 / exp coding
	for (int n = 1; n < nmax; n++) {
		for (int bpoint = 0; bpoint < nmax; bpoint++) {
			double val = decodebinarydot(n, bpoint);
			int p = encodebinarydot(val, bpoint);
			printf("n %d bpoint %d val %lg p %d\n", n, bpoint, val, p);
			if (p != n) printf("ERROR: n %d p %d\n", n, p);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

char *encode_Sydney_Opera_House() {	//	Sydney Opera House example
	latitude = -33.8570095;
	longitude = 151.2152005;
	altitude = 11.2;	// meter
	latitude_uncertainty = 0.0007105;
	longitude_uncertainty = 0.0007055;
	altitude_uncertainty = 33.7;
	expected_to_move = 0;
	sta_floor = 0;
	sta_height_above_floor = 0;
	sta_height_above_floor_uncertainty = 0.0078125;
	return encodeLCIstring();
}

void decode_Sydney_Opera_House() {
	const char *lcistr=lci2;
	if (verboseflag) printf("-lci=%s\n", lcistr);
	decodeLCIstring(lcistr);
}

void do_Sydney_Opera(void) {
	if (verboseflag) printf("Decode Syndney Opera House\n");
	decode_Sydney_Opera_House();

	if (verboseflag) printf("Encode Syndney Opera House\n");
	char *str = encode_Sydney_Opera_House();
	if (verboseflag) printf("-lci=%s\n", str);

	if (verboseflag) printf("Decode new Syndney Opera House\n");
	decodeLCIstring(str);

	free(str);
}

char *encode_US_MTV() {
	latitude = 37.41994;
	longitude = -122.075;	
	latitude_uncertainty = 0.000976563;	
	longitude_uncertainty = 0.000976563;
	altitude = 7.0;
	altitude_uncertainty = 64;
	expected_to_move = 0;
	sta_floor = 0;
	sta_height_above_floor = 0;
	sta_height_above_floor_uncertainty = 0.0078125;
	return encodeLCIstring();	
}

void decode_US_MTV() {
	const char *lcistr=lci3;
	if (verboseflag) printf("-lci=%s\n", lcistr);
	decodeLCIstring(lcistr);
}

void do_US_MTV(void) {
	if (verboseflag) printf("Decode US MTV\n");
	decode_US_MTV();

	if (verboseflag) printf("Encode US MTV\n");
	char *str = encode_US_MTV();
	if (verboseflag) printf("-lci=%s\n", str);

	if (verboseflag) printf("Decode new US MTV\n");
	decodeLCIstring(str);

	free(str);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void showusage(void) {
	printf("-v\t\tFlip verbose mode %s\n", verboseflag ? "off":"on");
	printf("-t\t\tFlip trace mode %s\n", traceflag ? "off":"on");
	printf("-d\t\tFlip debug mode %s\n", debugflag ? "off":"on");
	printf("-c\t\tFlip checking mode %s\n", checkflag ? "off":"on");
	printf("\n");
	printf("-lci=...\tDecode given LCI string\n");
	printf("\n");
	printf("To encode an LCI string use the following:\n");
	printf("\n");
	printf("-lat=...\tLatitude  (degrees)\n");
	printf("-lon=...\tLongitude (degrees)\n");
	printf("-alt=...\tAltitude  (%s)\n", altitude_type_string(Altitude_Type));
	printf("\n");
	printf("-latunc=...\tLatitude  uncertainty (degrees)\n");
	printf("-lonunc=...\tLongitude uncertainty (degrees)\n");
	printf("-altunc=...\tAltitude  uncertainty (%s)\n", altitude_type_string(Altitude_Type));
	printf("\n");
	printf("-smallest\tZero uncertainty means smallest (as opposed to unknown)\n"); 
	printf("\n");
	printf("-floor=...\tFloor number\n");
	printf("-height=...\tHeight above floor (m)\n");
	printf("-heightunc=...\tHeight above floor uncertainty (m)\n");
	printf("\n");
	printf("-BSSID=... \tMac Addresses of colocated BSSIDs (comma separated list)\n");
	printf("\n");
	printf("-datum=...\tCoordinate Datum   (default %d -> \"%s\")\n", datum, datum_string(datum));
	printf("-altitude_type=\tAltitude Type Code (default %d -> \"%s\")\n",
		   Altitude_Type, altitude_type_string(Altitude_Type));
	printf("\n");
	if (debugflag) { //	Shouldn't really fiddle with the following 
		printf("-Retransmission_Allowed\t\tRetransmission allowed (default %d)\n", retransmission_allowed);
		printf("-Retention_Expires_Present\tRetention Expires Present (default %d)\n", retention_expires_present);
		printf("-expiration=...\t\t\tExpiration (default %d hours)\n", expiration);
		printf("-STA_Location_Policy\t\tSTA Location Policy (default %d)\n", STA_location_policy);
		printf("-movable=...\t\t\tExpected to move (default %d)\n",	expected_to_move); // ?
		printf("\n");
	}
	printf("-sample\t\tShow example decoding / encoding\n");
	printf("-?\t\tPrint this command line argument summary\n");
	printf("-version=...\t%s\n", version);
	fflush(stdout);
	exit(1);
}

int commandline(int argc, const char *argv[]) {
	int firstarg = 1;
	while (firstarg < argc && *argv[firstarg] == '-') {
		const char *arg = argv[firstarg];
		if (strcmp(arg, "-v") == 0) verboseflag = !verboseflag;
		else if (strcmp(arg, "-t") == 0) traceflag = !traceflag;
		else if (strcmp(arg, "-d") == 0) debugflag = !debugflag;
		else if (strcmp(arg, "-c") == 0) checkflag = !checkflag;
		else if (strcmp(arg, "-smallest") == 0) smallestflag = !smallestflag;
		else if (strcmp(arg, "-sample") == 0) sampleflag = !sampleflag;
		else if (_strnicmp(arg, "-lci=", 5) == 0)		// string to decode (uc or lc)
			lcistring = arg + 5;
//		parameters for construction of LCI subelement 
		else if (strncmp(arg, "-lat=", 5) == 0) {	// in degrees
			if (sscanf_s(arg + 5, "%lg", &latitude) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-latitude=", 10) == 0) {
			if (sscanf_s(arg + 10, "%lg", &latitude) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-lon=", 5) == 0) {	// in degrees
			if (sscanf_s(arg + 5, "%lg", &longitude) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-longitude=", 11) == 0) {
			if (sscanf_s(arg + 11, "%lg", &longitude) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-alt=", 5) == 0) {	// in meters
			if (sscanf_s(arg + 5, "%lg", &altitude) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-altitude=", 10) == 0) {
			if (sscanf_s(arg + 10, "%lg", &altitude) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-latunc=", 8) == 0) {	// in degrees
			if (sscanf_s(arg + 8, "%lg", &latitude_uncertainty) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-lonunc=", 8) == 0) {	// in degrees
			if (sscanf_s(arg + 8, "%lg", &longitude_uncertainty) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-altunc=", 8) == 0) {	// in meters
			if (sscanf_s(arg + 8, "%lg", &altitude_uncertainty) < 1) printf("ERROR: %s\n", arg);
		}
//		parameters for construction of Z subelement 
		else if (strncmp(arg, "-floor=", 7) == 0) {	// need not be integer
			if (sscanf_s(arg + 7, "%lg", &sta_floor) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-height=", 8) == 0) { 
			if (sscanf_s(arg + 8, "%lg", &sta_height_above_floor) < 1) printf("ERROR: %s\n", arg);
		}
		else if (strncmp(arg, "-heightunc=", 11) == 0) {	// in meters
			if (sscanf_s(arg + 11, "%lg", &sta_height_above_floor_uncertainty) < 1) printf("ERROR: %s\n", arg);
		}
//		parameters for construction of colocated BSSIDs subelement
		else if (_strnicmp(arg, "-BSSID=", 7) == 0) {	// colocated BSSID
			extractBSSID(arg);
		}
//		Should not normally use or need any of the following:
		else if (_strnicmp(arg, "-altitude_type=", 15) == 0) {	// prefer meters
			if (sscanf_s(arg + 15, "%d", &Altitude_Type) < 1) printf("ERROR: %s\n", arg);
		}
		else if (_strnicmp(arg, "-datum=", 7) == 0) {	// default WGS84
			if (sscanf_s(arg + 7, "%d", &datum) < 1) printf("ERROR: %s\n", arg);
		}
		else if (_strnicmp(arg, "-expiration=", 12) == 0) {	// in hours - should be zero
			if (sscanf_s(arg + 12, "%d", &expiration) < 1) printf("ERROR: %s\n", arg);
			else retention_expires_present = 1;
		}
		else if (_strnicmp(arg, "-version=", 9) == 0) {	// should always be LCI_Version_1
			if (sscanf_s(arg + 9, "%d", &LCI_version) < 1) printf("ERROR: %s\n", arg);
		}
//		More obscure ones should not normally use or need any of them:
		else if (strcmp(arg, "-movable") == 0) expected_to_move = !expected_to_move;	// 0
		else if (strcmp(arg, "-Retransmission_Allowed") == 0) retransmission_allowed = ! retransmission_allowed; // 1
		else if (strcmp(arg, "-Retention_Expires_Present") == 0) retention_expires_present = ! retention_expires_present; // 0
		else if (strcmp(arg, "-STA_Location_Policy") == 0) STA_location_policy = ! STA_location_policy;
		else if (strcmp(arg, "-RegLoc_Agreement") == 0)	RegLoc_Agreement = !RegLoc_Agreement;
		else if (strcmp(arg, "-RegLoc_DSE") == 0) RegLoc_DSE = !RegLoc_DSE;
		else if (strcmp(arg, "-Dependent_STA") == 0) Dependent_STA = ! Dependent_STA;
//		Control which of the subelements gets encoded - not normally changed
		else if (strcmp(arg, "-want_LCI") == 0) wantLCIflag = ! wantLCIflag;
		else if (strcmp(arg, "-want_Z") == 0) wantZflag = ! wantZflag;
		else if (strcmp(arg, "-want_Usage") == 0) wantUsageflag = ! wantUsageflag;
		else if (strcmp(arg, "-want_Colocated") == 0) wantColocatedflag = ! wantColocatedflag;
		else if (strcmp(arg, "-?") == 0) showusage();
		else if (strcmp(arg, "-help") == 0) showusage();
		else if (strcmp(arg, "-version") == 0) printf("%s %s\n", "LCIcoder", version);
		else if (strcmp(arg, "-copyright") == 0) printf("%s %s\n", "LCIcoder", copyright);
		else printf("ERROR: %s\n", arg);
		firstarg++;
	}
	if (firstarg != argc) {
		printf("ERROR: unmatched command line argument: %s\n", argv[firstarg]);
	}
	checksettings();	// check compatibility with rules in Android Q
	return firstarg;
}

///////////////////////////////////////////////////////////////////////////////////////////////

void freeColocatedBSSIDs () {
	for (int k = 0; k < bssid_index; k++) {
		if (BSSIDS[k] == NULL) continue;
		free((void *) BSSIDS[k]);
		BSSIDS[k] = NULL;
	}
	free(BSSIDS);
	BSSIDS = NULL;
}

void initialize_arrays (void) {
	BSSIDS = (char const **) malloc((max_bssids+1) * sizeof(char *));
	if (BSSIDS == NULL) exit(1);
//	for (int k = 0; k <= MAX_BSSID; k++) BSSIDS[k] = NULL;	// or...
	memset(BSSIDS, 0, (max_bssids+1) * sizeof(char *));
}

int main(int argc, const char *argv[]) {
	int firstarg = 1;

//	testbinarydot(20);	return 0;	// testing
	initialize_arrays();
	firstarg = commandline(argc, argv);

//	Is LCI string given on command line ?
	if (lcistring != NULL) {	
		decodeLCIstring(lcistring);
		if (checkflag) {
			printf("\n");
			char *str = encodeLCIstring();	
			printf("lci=%s\n", str);
			free(str);
		}
	}

//	Are arguments for constructing LCI string given on command line ?
	else if (latitude != 0 || longitude != 0 || altitude != 0 ||
		  sta_floor != 0 || sta_height_above_floor!= 0 || sta_height_above_floor_uncertainty != 0 ||
		  bssid_index > 0 ) { // is some LCI information given on command line?
		if (bssid_index > 0) showColocatedBSSIDs();
		char *str = encodeLCIstring();	// use LCI parameters set from command line
		printf("lci=%s\n", str);
		if (checkflag) {	// check by decoding again ?
			printf("\n");
			decodeLCIstring(str);
		}
		free(str);
	}
	else if (sampleflag) {	// run an example...
		do_Sydney_Opera();
//		do_US_MTV();	// alternate example
	}

	freeColocatedBSSIDs();
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

// LCI element:  LCI subelement, Z subelement, USAGE subelement, BSSIDS subelements.

// CIVIC element:  STA location address, MAP image subelements.

///////////////////////////////////////////////////////////////////////////////

// LCIcoder -? shows command line flags and command line value usage

///////////////////////////////////////////////////////////////////////////////

// Sample usage for decoding
// LCIcoder -lci=010008001052834d12efd2b08b9b4bf1cc2c0000410406000000000010060101
// 01 00 08  00 10 51834d12efd1b08b9b4bf1cc2c000041   04 06 0000 000000 10  06 01 01

// LCIcoder -lci="010008001052834d12efd2b08b9b4bf1cc2c00004106030100000406000000000012"
// 01 00 08  00 10 52834d12efd2b08b9b4bf1cc2c000041  06 03 01 0000 04 06 000000000012

// Sydney Opera House encoding (with uncertainties)
// LCIcoder -lat=-33.8570095 -lon=151.2152005 -alt=11.1992  -latunc=0.000976563 -lonunc=0.000976563 -altunc=64 -floor=0 -height=0  -heightunc=0.03125

// Sample usage for encoding (without specifying uncertainties)
// LCIcoder -lat=-33.8570095 -lon=151.2152005 -alt=11.1992 

// MIT CSAIL STATA CENTER:
// LCIcoder -lat=42.3616375‬ -lon=-71.09063 -alt=20 -latunc=0.00063 -lonunc=0.00078 -altunc=15
// lci=010008001052234a2e15923c6674dc1101500000410406000000000000060101

// Compulab:
// LCIcoder -lat=32.659385 -lon=35.0997755 -alt=50 -latunc=0.00028 -lonunc=0.00040 -altunc=10
// lci=010008001053ba6654109371c58c111101c80000410406000000000000060101

////////////////////////////////////////////////////////////////////////////////////////
