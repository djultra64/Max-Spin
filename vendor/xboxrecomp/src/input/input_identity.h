/* Physical input identity shared by reports and prompt consumers. djultra64. */
#ifndef XBOX_INPUT_IDENTITY_H
#define XBOX_INPUT_IDENTITY_H
#include <stdint.h>
#define XBOXRECOMP_INPUT_IDENTITY 1
enum InputDeviceKind {INPUT_DEVICE_NONE,INPUT_DEVICE_PAD,INPUT_DEVICE_KEYBOARD};
enum InputFamily {INPUT_FAMILY_AUTO=-1,INPUT_FAMILY_UNKNOWN,INPUT_FAMILY_KEYBOARD,
                  INPUT_FAMILY_XBOX,INPUT_FAMILY_PLAYSTATION,INPUT_FAMILY_NINTENDO};
enum InputFaceLabel {INPUT_LABEL_UNKNOWN,INPUT_LABEL_A,INPUT_LABEL_B,INPUT_LABEL_X,
                    INPUT_LABEL_Y,INPUT_LABEL_CROSS,INPUT_LABEL_CIRCLE,
                    INPUT_LABEL_SQUARE,INPUT_LABEL_TRIANGLE};
typedef struct InputDeviceIdentity {
    uint64_t generation;
    uint32_t instance_id;
    int kind,slot,family,detected_family;
    uint8_t face_label[4]; /* SOUTH, EAST, WEST, NORTH; positions never swap. */
    char name[64];
} InputDeviceIdentity;
static inline int input_family_face_label(int family,unsigned position){
    static const uint8_t xbox[]={INPUT_LABEL_A,INPUT_LABEL_B,INPUT_LABEL_X,INPUT_LABEL_Y};
    static const uint8_t ps[]={INPUT_LABEL_CROSS,INPUT_LABEL_CIRCLE,INPUT_LABEL_SQUARE,INPUT_LABEL_TRIANGLE};
    static const uint8_t sw[]={INPUT_LABEL_B,INPUT_LABEL_A,INPUT_LABEL_Y,INPUT_LABEL_X};
    if(position>=4)return INPUT_LABEL_UNKNOWN;
    return family==INPUT_FAMILY_XBOX?xbox[position]:family==INPUT_FAMILY_PLAYSTATION?ps[position]:family==INPUT_FAMILY_NINTENDO?sw[position]:INPUT_LABEL_UNKNOWN;
}
#endif
