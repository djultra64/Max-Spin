#include "actor_pb_adapter.h"
#include <string.h>
int vadb_vpli_staged(VpliBridge*b,VpliSceneTag*out){
 if(!b||!out)return 0;memset(out,0,sizeof*out);
 if(!b->configured||!b->enabled||atomic_load(&b->failed))return 0;
 pthread_mutex_lock(&b->mutex);VpliSceneTag t=b->staged_scene;
 int ok=!atomic_load(&b->failed)&&t.version==1&&t.scene&&t.view&&t.authority_revision&&b->config.authority_revision(b->config.user)==t.authority_revision;
 if(ok)*out=t;pthread_mutex_unlock(&b->mutex);return ok;
}
