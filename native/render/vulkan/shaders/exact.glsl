// N7b: aritmetica do modo fiel, comum a pvr.frag e resolve.comp (#include). O software (runtime/pvr/pvr_render.cpp)
// roda com arredondamento ao par mais proximo e sem descartar subnormais (MXCSR 0x1F80, RenderFpEnv).
//   - FLOAT_CONTROLS (RTE em float32) e DENORM_PRESERVE (subnormais preservados em float32): as variantes para
//     dispositivos com VK_KHR_shader_float_controls que oferecem cada garantia; sem elas, vale o padrao do dispositivo
//     (o backend informa qual garantia existe);
//   - k255: c / 255.f do software, exato (gerado em float32 e conferido contra a divisao float32 corretamente
//     arredondada nos 256 valores);
//   - crdiv: a / b corretamente arredondado por divisao INTEIRA das mantissas (nao depende do FMA nem do 1/x da GPU,
//     que tem 2,5 ULP); subnormais e estouro voltam para a divisao do dispositivo (fora do que o jogo produz).
#ifdef FLOAT_CONTROLS
#extension GL_EXT_spirv_intrinsics : require
spirv_execution_mode(extensions = ["SPV_KHR_float_controls"], capabilities = [4467], 4462, 32);  // RoundingModeRTE
#ifdef DENORM_PRESERVE
spirv_execution_mode(extensions = ["SPV_KHR_float_controls"], capabilities = [4464], 4459, 32);  // DenormPreserve
#endif
#endif

const float k255[256] = float[256](
    0, 0.00392156886, 0.00784313772, 0.0117647061, 0.0156862754, 0.0196078438, 0.0235294122, 0.0274509806,
    0.0313725509, 0.0352941193, 0.0392156877, 0.0431372561, 0.0470588244, 0.0509803928, 0.0549019612, 0.0588235296,
    0.0627451017, 0.0666666701, 0.0705882385, 0.0745098069, 0.0784313753, 0.0823529437, 0.0862745121, 0.0901960805,
    0.0941176489, 0.0980392173, 0.101960786, 0.105882354, 0.109803922, 0.113725491, 0.117647059, 0.121568628,
    0.125490203, 0.129411772, 0.13333334, 0.137254909, 0.141176477, 0.145098045, 0.149019614, 0.152941182,
    0.156862751, 0.160784319, 0.164705887, 0.168627456, 0.172549024, 0.176470593, 0.180392161, 0.184313729,
    0.188235298, 0.192156866, 0.196078435, 0.200000003, 0.203921571, 0.20784314, 0.211764708, 0.215686277,
    0.219607845, 0.223529413, 0.227450982, 0.23137255, 0.235294119, 0.239215687, 0.243137255, 0.247058824,
    0.250980407, 0.254901975, 0.258823544, 0.262745112, 0.266666681, 0.270588249, 0.274509817, 0.278431386,
    0.282352954, 0.286274523, 0.290196091, 0.294117659, 0.298039228, 0.301960796, 0.305882365, 0.309803933,
    0.313725501, 0.31764707, 0.321568638, 0.325490206, 0.329411775, 0.333333343, 0.337254912, 0.34117648,
    0.345098048, 0.349019617, 0.352941185, 0.356862754, 0.360784322, 0.36470589, 0.368627459, 0.372549027,
    0.376470596, 0.380392164, 0.384313732, 0.388235301, 0.392156869, 0.396078438, 0.400000006, 0.403921574,
    0.407843143, 0.411764711, 0.41568628, 0.419607848, 0.423529416, 0.427450985, 0.431372553, 0.435294122,
    0.43921569, 0.443137258, 0.447058827, 0.450980395, 0.454901963, 0.458823532, 0.4627451, 0.466666669,
    0.470588237, 0.474509805, 0.478431374, 0.482352942, 0.486274511, 0.490196079, 0.494117647, 0.498039216,
    0.501960814, 0.505882382, 0.509803951, 0.513725519, 0.517647088, 0.521568656, 0.525490224, 0.529411793,
    0.533333361, 0.53725493, 0.541176498, 0.545098066, 0.549019635, 0.552941203, 0.556862772, 0.56078434,
    0.564705908, 0.568627477, 0.572549045, 0.576470613, 0.580392182, 0.58431375, 0.588235319, 0.592156887,
    0.596078455, 0.600000024, 0.603921592, 0.607843161, 0.611764729, 0.615686297, 0.619607866, 0.623529434,
    0.627451003, 0.631372571, 0.635294139, 0.639215708, 0.643137276, 0.647058845, 0.650980413, 0.654901981,
    0.65882355, 0.662745118, 0.666666687, 0.670588255, 0.674509823, 0.678431392, 0.68235296, 0.686274529,
    0.690196097, 0.694117665, 0.698039234, 0.701960802, 0.70588237, 0.709803939, 0.713725507, 0.717647076,
    0.721568644, 0.725490212, 0.729411781, 0.733333349, 0.737254918, 0.741176486, 0.745098054, 0.749019623,
    0.752941191, 0.75686276, 0.760784328, 0.764705896, 0.768627465, 0.772549033, 0.776470602, 0.78039217,
    0.784313738, 0.788235307, 0.792156875, 0.796078444, 0.800000012, 0.80392158, 0.807843149, 0.811764717,
    0.815686285, 0.819607854, 0.823529422, 0.827450991, 0.831372559, 0.835294127, 0.839215696, 0.843137264,
    0.847058833, 0.850980401, 0.854901969, 0.858823538, 0.862745106, 0.866666675, 0.870588243, 0.874509811,
    0.87843138, 0.882352948, 0.886274517, 0.890196085, 0.894117653, 0.898039222, 0.90196079, 0.905882359,
    0.909803927, 0.913725495, 0.917647064, 0.921568632, 0.925490201, 0.929411769, 0.933333337, 0.937254906,
    0.941176474, 0.945098042, 0.949019611, 0.952941179, 0.956862748, 0.960784316, 0.964705884, 0.968627453,
    0.972549021, 0.97647059, 0.980392158, 0.984313726, 0.988235295, 0.992156863, 0.996078432, 1);

float crdiv(float a, float b) {
    const uint ua = floatBitsToUint(a), ub = floatBitsToUint(b);
    const int  ea = int((ua >> 23) & 0xFFu), eb = int((ub >> 23) & 0xFFu);
    if (ea == 0 || eb == 0 || ea == 255 || eb == 255) return a / b;  // zero, subnormal, inf, NaN
    uint      ma = (ua & 0x7FFFFFu) | 0x800000u, mb = (ub & 0x7FFFFFu) | 0x800000u;
    int       e  = ea - eb + 127;
    if (ma < mb) ma <<= 1, e -= 1;  // agora mb <= ma < 2 mb: o quociente tem o bit 23 ligado
    // quociente de 24 bits por subtracao (resto < mb < 2^24; resto << 1 cabe em 32 bits)
    uint q = 0u, r = ma;
    for (int i = 0; i < 24; ++i) {
        q <<= 1;
        if (r >= mb) r -= mb, q |= 1u;
        r <<= 1;
    }
    // arredondamento ao par: r ja esta dobrado (r = 2 * resto); compara com mb
    if (r > mb || (r == mb && (q & 1u) != 0u)) {
        q += 1u;
        if (q == 0x1000000u) q >>= 1, e += 1;
    }
    if (e <= 0 || e >= 255) return a / b;  // subnormal ou estouro no resultado
    return uintBitsToFloat(((ua ^ ub) & 0x80000000u) | (uint(e) << 23) | (q & 0x7FFFFFu));
}

// lround(clamp01(v) * 255): v * 255 < 256 tem ulp >= 2^-16, a soma de 0.5 e exata
uint q8(float v) {
    // NaN (1/w = 0 na divisao da perspectiva) vira 0: clamp(NaN) e indefinido e o indice sairia da tabela k255 (memoria
    // privada fora dos limites derruba a GPU AMD); o software, com NaN, tambem nao produz uma cor definida
    if (!(v >= 0.0)) return 0u;
    precise float x = min(v, 1.0) * 255.0;
    return min(uint(floor(x + 0.5)), 255u);
}
vec4 quant(vec4 c) { return vec4(k255[q8(c.r)], k255[q8(c.g)], k255[q8(c.b)], k255[q8(c.a)]); }  // pack + unpack
