# Añadir tenistas mediante paquetes nativos

Trabajo propio: **djultra64**.

Cada carpeta numerada sustituye una identidad que ya existe en el juego. Puedes
añadir un paquete sin recompilar el ejecutable. El sistema no aumenta el número
de plazas del selector ni convierte automáticamente modelos de PC al formato
Xbox. Una imagen de retrato por sí sola tampoco crea un modelo jugable.

Los comandos de esta guía se ejecutan desde la carpeta que contiene
`native-linux/`, `assets/` y `characters/`. Necesitas Python 3.10 o posterior en
Linux. La preparación y validación no arrancan el juego ni modifican las
partidas guardadas.

## Elegir la plaza

Consulta el catálogo antes de preparar archivos:

```sh
python3 native-linux/tools/character_package_tool.py catalog
```

`slots.json` contiene la misma asignación y la evidencia de origen. En hombres,
las carpetas 1–8 corresponden a la fila superior original: Sampras, Hewitt,
Gambill, Grosjean, Kuerten, Chang, Blake y Robredo. Las carpetas 9–16 son la fila
inferior original: Tukur, Houston, Wang, Koenig, Gide, Rios, Emory y Nash.
La revisión privada usa esas ocho últimas para Federer, Nadal, Djokovic,
Agassi, Becker, McEnroe, Pioline y Haas. Esta distribución pública mantiene
las 32 carpetas vacías y conserva los tenistas originales. Guarda una copia
de cualquier paquete privado antes de sustituirlo o cambiar sus retratos.

La reserva femenina es:

| Carpeta | Identidad nativa | ID de base de datos |
| --- | --- | --- |
| 17 | HingisX | 516 |
| 18 | AnnaXXX | 517 |
| 19 | Barbara | 518 |
| 20 | Daniela | 519 |
| 21 | Dementi | 520 |
| 22 | Coetzer | 521 |
| 23 | Harkler | 522 |
| 24 | Shaughn | 523 |
| 25 | Yamanix | 524 |
| 26 | Baretto | 525 |
| 27 | Tellawi | 526 |
| 28 | Abercro | 527 |
| 29 | Auyeung | 528 |
| 30 | Hammond | 529 |
| 31 | sergeye | 530 |
| 32 | Hillxxx | 531 |

**Esta asignación femenina es provisional y sigue los IDs nativos. Su orden
visual todavía no se ha verificado.** No se ha usado el orden alfabético de
`playerlist.pml` como prueba del selector. Las posiciones de pantalla femeninas
figuran como desconocidas en el catálogo; no hay nuevos paquetes femeninos
incluidos. Los paquetes de mujeres usan exclusivamente `players(w).pml` y
`profils(w).pml`; los de hombres, los equivalentes `(m)`.

La reserva de 32 identidades no significa que ya estén admitidas todas las
variantes históricas de mallas y texturas. En la comprobación estructural de
originales femeninos, los vestuarios completos de las plazas 17, 19, 22, 23 y 24
son compatibles con el lector actual. Las plazas 18 y 20 contienen alguna
variante de vértices todavía no admitida; 21 contiene un formato de textura
adicional, y 25–32 incluyen tipos de recursos originales todavía restringidos
en paquetes nuevos. Un paquete futuro para esas identidades necesita recursos
convertidos a los formatos admitidos o ampliar y verificar primero el lector.
Mientras no supere la validación, conserva la atleta original. No se omiten
comprobaciones para activar una plantilla.

## Crear una plantilla privada

Este ejemplo prepara la plaza femenina 17 en una carpeta de trabajo separada:

```sh
python3 native-linux/tools/character_package_tool.py template \
  --assets assets --output-parent athlete-drafts --slot 17 --name "Mi tenista"
```

El resultado es:

```text
athlete-drafts/
  17/
    manifest.template.json
    outfit0.wad
    outfit1.wad
```

Los dos WAD se copian de tus originales y siguen mostrando a la atleta original
hasta que prepares los reemplazos. La plantilla queda desactivada: no existe
`manifest.json`, y `compatibility.validated` es `false`. Nunca sobrescribe una
carpeta existente. No copies la plantilla a `characters/` esperando un rostro
nuevo: primero completa los recursos y los metadatos.

El manifiesto usa `maxspin-character-v1` y contiene:

- `slot`, `target` y `gender`: plaza numérica, identidad nativa exacta y familia
  `male` o `female`. No cambies `target` para poner el nombre del nuevo tenista.
- `name` y `author`: nombre descriptivo del paquete y autor. Para el trabajo
  propio de este proyecto usa `djultra64`; conserva créditos de terceros cuando
  corresponda.
- `files`: exactamente los dos WAD de vestuario del mismo destino, con ruta de
  archivo dentro del paquete y SHA256.
- `base_files`: SHA256 de los originales concretos contra los que se preparó
  el paquete. No son archivos originales que debas incluir en el paquete.
- `pml_updates`: cambios individuales de propiedades de `Db.wad`.
- `member_updates`: reemplazos individuales permitidos dentro de `GameIni.wad`.
- `compatibility`: plataforma `xbox`, estado de validación y evidencia concreta.
  Actualizar hashes no certifica compatibilidad ni cambia este estado.

## Recursos necesarios

Cada `outfit0.wad` / `outfit1.wad` debe mantener el inventario de su WAD Xbox
original. Para Hingis los miembros pertenecen a
`media/gamepart/players/hingisx00/` y `hingisx01/`, respectivamente:

| Recurso nativo | Requisito |
| --- | --- |
| `playerlod00.ooo` | Modelo de máximo detalle con cabeza, cuerpo, pelo y vestuario coherentes. |
| `playerlod01.ooo` | Modelo de menor detalle que represente a la misma persona, pelo y accesorios. |
| `textures/tex.pak` | Todas las texturas que utilizan ambos modelos, incluidos pelo y ropa. |
| `player.ini` | Conservar dependencias, secciones y claves. Solo se permiten los nombres de `General` y desactivar `Accessories/Hair` mediante cadena vacía si el pelo ya está integrado. |
| `scorename.dds` | Nombre dibujado en el marcador, presente en ambos vestuarios. Conservar cabecera, dimensiones, formato y longitud nativos. |
| `soundscore.pml`, `soundevent.pml` | Mantener los miembros y sus dependencias de audio originales. |

El resto del inventario se conserva. No basta con exportar un único modelo de
alta calidad ni con cambiar solo un vestuario. El pelo integrado debe existir
en ambos LOD; quitar un accesorio sin añadir el pelo correspondiente al modelo
puede producir una cabeza incompleta. La ropa requiere geometría y texturas de
la misma familia nativa; no intercambies recursos masculinos y femeninos por ID.

El archivo de menor detalle también sirve al detalle lejano. Si integras pelo
rígido del creador, conserva las dos identidades nativas: `M_ACC_CHV_______000_02`
y `M_ACC_CHV_______000_03` (prefijo `F_` para mujeres). Ambas deben contener el
peinado elegido y sus accesorios. El motor oculta «02» cuando selecciona «03»;
eliminar la variante lejana deja la cabeza sin la cobertura opaca. La utilidad
`hair_lod_variants.ensure_model_variants` copia el estilo elegido a la identidad
lejana y conserva texturas y geometría de los demás objetos. El validador de
paquetes rechaza una pareja integrada ausente o con estilos distintos.

El validador revisa modelos, huesos, recursos, texturas, dependencias y colisiones
entre paquetes. Cuando crees nuevas texturas, asigna identidades que no entren
en conflicto con otras plazas; `character_formats.namespace_textures` es la
utilidad técnica existente para reconstruir modelos y paquete de texturas de
forma conjunta. Cambiar solo los IDs del `tex.pak` deja referencias sin resolver.
Las texturas idénticas sí pueden compartir ID. La herramienta de plantillas no
es un modelador, editor de mallas ni conversor de formatos.

Para los retratos, prepara dos imágenes cuadradas independientes, preferiblemente
PNG de 512 × 512 con fondo transparente: pose de medio cuerpo y recorte de cara.
Los miembros que finalmente consume el juego dentro de `GameIni.wad` son:

- `vignette1.dds`: **256 × 256**, tarjeta/pose.
- `vignettevisage.dds`: **64 × 64**, solo cara. Este es el nombre nativo completo.

Ambos mantienen la cabecera DDS original y el mismo tamaño de datos. Cada
miembro se declara por separado en `member_updates`, con la ruta nativa del
destino y vestuario que realmente exista en el archivo original. No inventes
retratos del vestuario alterno si no aparecen en su inventario. Conserva el
envoltorio PAM-ZLB original cuando el miembro lo tenga. `pack_zlb` y
`rebuild_pam` de `character_formats.py` permiten conservar estos contratos.
Copiar un PNG y renombrarlo `.dds` no realiza la conversión.

Los nombres del marcador se preparan como bitmap con la misma tipografía nativa
NovaBlackSSK en cursiva que usan los paquetes actuales; `scoreboard_font.py`
reutiliza el atlas original privado. Cambiar un nombre de texto en la base de
datos no cambia automáticamente este bitmap. El editor de caras existente
mantiene su alcance de los personajes ya configurados; este formato de paquetes
no promete un editor gráfico general para futuras atletas.

## Metadatos por propiedad

Ejemplo femenino para el `pml_updates` de la plaza 17:

```json
[
  {
    "archive": "Media/Wads/Db.wad",
    "member": "media/db/playerlist.pml",
    "node": "TPLayerInfo/DirName=HingisX00",
    "property": "Name",
    "value": "Mi tenista"
  },
  {
    "archive": "Media/Wads/Db.wad",
    "member": "media/db/players(w).pml",
    "node": "TRPlayer/Id=516",
    "property": "Name",
    "value": "Mi tenista"
  },
  {
    "archive": "Media/Wads/Db.wad",
    "member": "media/db/profils(w).pml",
    "node": "TRPlayer/Id=516",
    "property": "Name",
    "value": "Mi tenista"
  }
]
```

Este fragmento ilustra el esquema; completa también el `Name`/`ShortName` de
`HingisX01`, los nombres de los dos `player.ini` y la identidad `Firstname` /
`Lastname` / `Country` de las filas `TRPlayer` que necesites. Así evitas textos
mezclados entre selector, ficha, carrera y partido. Las propiedades deben existir
en el original; no se añaden tablas, filas ni claves nuevas.

La lista del selector permite `Name`, `ShortName`, `Available`,
`FromPlayerCreator` y `RightHanded`. Conserva `FromPlayerCreator=0` para los
modelos nativos integrados. `IsWoman`, `DirName`, `DBName`, `CodeName` e `Id`
no se reescriben. Las filas de jugador admiten nombres y país, edad 15–65,
altura 120–230 cm, peso 30–180 kg, lateralidad 0/1, estilo de saque 0–3 y
talento 0–2; `ZoneID` 0–8 y `BirthplaceID` 0–1000 conservan el catálogo nativo.
Usa país, token de país, ciudad e identificadores coherentes del original:
estar dentro del rango numérico no garantiza que exista una ciudad concreta.
`TPlayers/Id=516` en `profils(w).pml` admite los atributos numéricos existentes;
los nombres se cambian en `TRPlayer`.

Una habilidad existente usa un selector como
`TRPlayer/Id=516/Name=Forehand`, propiedad `Value`, entero entre 0 y 10. No se
admite cambiar `Endurance` por esta vía. Usa cadenas JSON para todos los valores,
incluidos números, por ejemplo `"24"`. Los textos deben ser representables en
Windows-1252 y tener como máximo 128 caracteres sin controles.

Ejemplo de un retrato individual en `member_updates`:

```json
[
  {
    "archive": "Media/Wads/GameIni.wad",
    "member": "media/gamepart/players/hingisx00/vignette1.dds",
    "path": "portraits/pose.zlb",
    "sha256": "REEMPLAZAR_MEDIANTE_REFRESH"
  }
]
```

La extensión del archivo de carga es libre; su contenido debe ser el miembro
nativo DDS/PAM-ZLB correcto. Añade otra entrada para la cara. No declares
`Db.wad` ni `GameIni.wad` completos en `files`: se rechazan para que varios
tenistas puedan modificar sus propias filas sin sobrescribirse entre sí.

## Actualizar hashes, validar e instalar

Después de editar los recursos y declarar los cambios:

```sh
python3 native-linux/tools/character_package_tool.py refresh \
  --assets assets --package athlete-drafts/17
```

Este comando calcula hashes de los archivos declarados, añade las vinculaciones
necesarias a los originales de `Db.wad` / `GameIni.wad` cuando hay cambios
individuales y conserva el estado de compatibilidad. Los originales deben ser
los archivos Xbox intactos de tu instalación; no apuntes `--assets` a una vista
temporal ya modificada. Opcionalmente puedes vincular `GamePart.wad` para la
comprobación de esqueleto y el WAD de ropa del género correspondiente como
fuente de preparación. No se copian esos WAD compartidos al paquete.

Cuando hayas confirmado la estructura Xbox de todos los recursos, registra una
descripción verificable en `compatibility.evidence`, cambia `validated` a `true`
y renombra `manifest.template.json` a `manifest.json` dentro de la carpeta de
trabajo. La evidencia debe distinguir validación estructural de comprobación
visual y de juego. Después ejecuta:

```sh
python3 native-linux/tools/character_package_tool.py validate \
  --assets assets --packages athlete-drafts
```

El informe JSON indica paquetes aceptados, rechazados y carpetas desactivadas.
Un rechazo devuelve código 1. `valid: true` con la plaza dentro de `disabled`
significa que aún no hay manifiesto activo: comprueba que aparezca en `accepted`.
El comando valida archivos y composición, sin arrancar el juego.

Para instalar, copia la carpeta numerada completa a `characters/`, conservando
primero una copia de cualquier paquete anterior de esa plaza. Vuelve a ejecutar
el validador con `--packages characters` para detectar también conflictos de
texturas con los demás tenistas instalados. Los cambios se aplican en el próximo
arranque. Si un paquete falla, se conserva la identidad original completa de
esa plaza y se emite un diagnóstico; las otras plazas válidas siguen activas.
Para desactivarlo, mueve `manifest.json` fuera de su carpeta numerada.

La comprobación final en juego la hace el usuario: selector y ficha, ambos
vestuarios, pelo y ropa a distintas distancias, retratos, marcador y lateralidad.
La validación automática no prueba animación, semejanza facial ni aceptación
con mandos reales. Los originales y las partidas permanecen separados de la
vista temporal privada del lanzador.

## Origen de los archivos y distribución

Necesitas tus recursos Xbox obtenidos legalmente para preparar y usar estos
paquetes. No se incluyen WAD originales, ejecutables, atlas privados, fotografías
ni material de terceros con permiso incierto en el código público o en una
plantilla pública. Las plantillas generadas localmente contienen copias privadas
de tus propios WAD y tampoco se deben publicar como si fueran recursos libres.
Usa imágenes y modelos propios o autorizados y conserva sus licencias y créditos.
Un paquete ya integrado con datos derivados del juego sigue siendo privado;
distribuir el código de las herramientas no concede derechos sobre esos datos.
