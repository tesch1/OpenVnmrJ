/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */

/*
  WARNING: THIS FILE IS AUTO-GENERATED. DO NOT MODIFY.

  This file was generated from Console_Stat.idl using "rtiddsgen".
  The rtiddsgen tool is part of the RTI Data Distribution Service distribution.
  For more information, type 'rtiddsgen -help' at a command shell
  or consult the RTI Data Distribution Service manual.
*/

#ifndef Console_StatPlugin_h
#define Console_StatPlugin_h

#ifndef Console_Stat_h
#include "Console_Stat.h"
#endif




struct RTICdrStream;
struct MIGRtpsGuid;

#ifndef pres_typePlugin_h
#include "pres/pres_typePlugin.h"
#endif


#ifdef __cplusplus
extern "C" {
#endif


/* The type used to store keys for instances of type struct
 * Console_Stat.
 *
 * By default, this type is struct Console_Stat
 * itself. However, if for some reason this choice is not practical for your
 * system (e.g. if sizeof(struct Console_Stat)
 * is very large), you may redefine this typedef in terms of another type of
 * your choosing. HOWEVER, if you define the KeyHolder type to be something
 * other than struct Console_Stat, the
 * following restriction applies: the key of struct
 * Console_Stat must consist of a
 * single field of your redefined KeyHolder type and that field must be the
 * first field in struct Console_Stat.
 */
typedef Console_Stat Console_StatKeyHolder;


extern RTIBool Console_StatPlugin_serialize(
    struct RTICdrStream *stream, const Console_Stat *sample,
    void *serialize_option);

extern RTIBool Console_StatPlugin_serialize_data(
    struct RTICdrStream *stream, const Console_Stat *sample,
    void *serialize_option);



extern RTIBool Console_StatPlugin_deserialize(
    struct RTICdrStream *stream, Console_Stat *sample,
    void *deserialize_option);

extern RTIBool Console_StatPlugin_deserialize_data(
    struct RTICdrStream *stream, Console_Stat *sample,
    void *deserialize_option);



extern unsigned int Console_StatPlugin_get_max_size_serialized(
    unsigned int current_alignment);

extern unsigned int Console_StatPlugin_get_max_size_serialized_data(
    unsigned int current_alignment);

extern void Console_StatPlugin_print(
    const Console_Stat *sample,
    const char *description, int indent_level);

extern Console_Stat *Console_StatPlugin_create_sample();

extern Console_Stat *Console_StatPlugin_create_sample_ex(RTIBool allocatePointers);        

extern void Console_StatPlugin_delete_sample(
    Console_Stat *sample);        

extern void Console_StatPlugin_delete_sample_ex(
    Console_Stat *sample,RTIBool deletePointers);

extern PRESTypePluginKeyKind Console_StatPlugin_get_key_kind();

extern struct PRESTypePlugin *Console_StatPlugin_new();

extern void Console_StatPlugin_delete(struct PRESTypePlugin *plugin);

/* The following are used if your key kind is USER_KEY */

extern Console_StatKeyHolder *Console_StatPlugin_create_key();

extern void Console_StatPlugin_delete_key(
    Console_StatKeyHolder *key);

extern RTIBool Console_StatPlugin_instance_to_key(
    Console_StatKeyHolder *key, const Console_Stat *instance);

extern RTIBool Console_StatPlugin_key_to_instance(
    Console_Stat *instance, const Console_StatKeyHolder *key);

extern RTIBool Console_StatPlugin_instance_to_id(
    DDS_InstanceId_t *id, RTIBool *is_unique,
    const Console_Stat *instance);




extern RTIBool Console_StatPlugin_serialize_key(
    struct RTICdrStream *stream, const Console_Stat *sample,
    void *serialize_option);

extern RTIBool Console_StatPlugin_deserialize_key(
    struct RTICdrStream *stream, Console_Stat *sample,
    void *deserialize_option);

extern unsigned int Console_StatPlugin_get_max_size_serialized_key(
    unsigned int current_alignment);

#ifdef __cplusplus
}
#endif
        

#endif /* Console_StatPlugin_h */
