/**
 *
 */
package cgEditor.editors;

/**
 * @author Martinez
 */
public class CgScanner extends ShaderFileScanner {
	static char escChar[] = { '\n', ' ', '.', ';', ',', '(', ')', '[', ']' };

	static final String language[] = { "bool", "const", "static", "uniform",
			"varying", "register", "in", "interface", "out", "void", "do",
			"while", "for", "if", "else", "typedef", "struct", "discard",
			"return" };

	static final String types[] = { "fixed", "fixed1", "fixed2", "fixed3",
			"fixed4", "half", "half1", "half2", "half3", "half4", "float",
			"float1", "float2", "float3", "float4", "float1x1", "float1x2",
			"float1x3", "float1x4", "float2x1", "float2x2", "float2x3",
			"float2x4", "float3x1", "float3x2", "float3x3", "float3x4",
			"float4x1", "float4x2", "float4x3", "float4x4", "samplerRECT",
			"samplerCUBE", "sampler3D", "sampler2D", "sampler1D" };

	static final String functions[] = { "_SEQ", "_SGE", "_SGT", "_SLE", "_SLT",
			"_SNE", "abs", "acos", "asin", "atan", "atan2", "ceil", "clamp",
			"cos", "cosh", "cross", "ddx", "ddy", "degrees", "dot", "exp",
			"exp2", "floor", "fmod", "frexp", "frac", "isfinite", "isinf",
			"isnan", "lit", "ldexp", "log", "log2", "log10", "max", "min",
			"mix", "mul", "lerp", "modf", "noise", "pow", "radians", "round",
			"rsqrt", "sign", "sin", "sinh", "smoothstep", "step", "sqrt",
			"tan", "tanh", "distance", "fresnel", "length", "normalize",
			"reflect", "reflectn", "refract", "refractn", "tex1D", "f1tex1D",
			"f2tex1D", "f3tex1D", "f4tex1D", "h1tex1D", "h2tex1D", "h3tex1D",
			"h4tex1D", "x1tex1D", "x2tex1D", "x3tex1D", "x4tex1D", "tex1Dbias",
			"tex2Dbias", "tex3Dbias", "texRECTbias", "texCUBEbias", "tex1Dlod",
			"tex2Dlod", "tex3Dlod", "texRECTlod", "texCUBElod", "tex1Dproj",
			"f1tex1Dproj", "f2tex1Dproj", "f3tex1Dproj", "f4tex1Dproj",
			"h1tex1Dproj", "h2tex1Dproj", "h3tex1Dproj", "h4tex1Dproj",
			"x1tex1Dproj", "x2tex1Dproj", "x3tex1Dproj", "x4tex1Dproj",
			"tex2D", "f1tex2D", "f2tex2D", "f3tex2D", "f4tex2D", "h1tex2D",
			"h2tex2D", "h3tex2D", "h4tex2D", "x1tex2D", "x2tex2D", "x3tex2D",
			"x4tex2D", "tex2Dproj", "f1tex2Dproj", "f2tex2Dproj",
			"f3tex2Dproj", "f4tex2Dproj", "h1tex2Dproj", "h2tex2Dproj",
			"h3tex2Dproj", "h4tex2Dproj", "x1tex2Dproj", "x2tex2Dproj",
			"x3tex2Dproj", "x4tex2Dproj", "tex3D", "f1tex3D", "f2tex3D",
			"f3tex3D", "f4tex3D", "h1tex3D", "h2tex3D", "h3tex3D", "h4tex3D",
			"x1tex3D", "x2tex3D", "x3tex3D", "x4tex3D", "tex3Dproj",
			"f1tex3Dproj", "f2tex3Dproj", "f3tex3Dproj", "f4tex3Dproj",
			"h1tex3Dproj", "h2tex3Dproj", "h3tex3Dproj", "h4tex3Dproj",
			"x1tex3Dproj", "x2tex3Dproj", "x3tex3Dproj", "x4tex3Dproj",
			"tex1CUBE", "f1texCUBE", "f2texCUBE", "f3texCUBE", "f4texCUBE",
			"h1texCUBE", "h2texCUBE", "h3texCUBE", "h4texCUBE", "x1texCUBE",
			"x2texCUBE", "x3texCUBE", "x4texCUBE", "texCUBEproj",
			"f1texCUBEproj", "f2texCUBEproj", "f3texCUBEproj", "f4texCUBEproj",
			"h1texCUBEproj", "h2texCUBEproj", "h3texCUBEproj", "h4texCUBEproj",
			"x1texCUBEproj", "x2texCUBEproj", "x3texCUBEproj", "x4texCUBEproj",
			"f1texCUBE", "f2texCUBE", "f3texCUBE", "f4texCUBE", "h1texCUBE",
			"h2texCUBE", "h3texCUBE", "h4texCUBE", "x1texCUBE", "x2texCUBE",
			"x3texCUBE", "x4texCUBE", "texRECT", "f1texRECT", "f2texRECT",
			"f3texRECT", "f4texRECT", "h1texRECT", "h2texRECT", "h3texRECT",
			"h4texRECT", "x1texRECT", "x2texRECT", "x3texRECT", "x4texRECT",
			"texRECTproj", "f1texRECTproj", "f2texRECTproj", "f3texRECTproj",
			"f4texRECTproj", "h1texRECTproj", "h2texRECTproj", "h3texRECTproj",
			"h4texRECTproj", "x1texRECTproj", "x2texRECTproj", "x3texRECTproj",
			"x4texRECTproj", "f1texRECT", "f2texRECT", "f3texRECT",
			"f4texRECT", "h1texRECT", "h2texRECT", "h3texRECT", "h4texRECT",
			"x1texRECT", "x2texRECT", "x3texRECT", "x4texRECT", "texcompare2D",
			"f1texcompare2D", "f1texcompare2D", "f1texcompare2D",
			"h1texcompare2D", "h1texcompare2D", "h1texcompare2D",
			"x1texcompare2D", "x1texcompare2D", "x1texcompare2D", "pack_2half",
			"unpack_2half", "pack_4clamp1s", "unpack_4clamp1s",
			"application2vertex", "vertex2fragment", "sincos" };

	static final String semantics[] = { "HPOS", "POSITION", "NORMAL", "PSIZ",
			"WPOS", "COLOR", "COLOR0", "COLOR1", "COLOR2", "COLOR3", "COL0",
			"COL1", "BCOL0", "BCOL1", "FOGP", "FOGC", "NRML", "TEXCOORD0",
			"TEXCOORD1", "TEXCOORD2", "TEXCOORD3", "TEXCOORD4", "TEXCOORD5",
			"TEXCOORD6", "TEXCOORD7", "TEX0", "TEX1", "TEX2", "TEX3", "TEX4",
			"TEX5", "TEX6", "TEX7", "DEPR", "DEPTH", "ATTR0", "ATTR1", "ATTR2",
			"ATTR3", "ATTR4", "ATTR5", "ATTR6", "ATTR7", "ATTR8", "ATTR9",
			"ATTR10", "ATTR11", "ATTR12", "ATTR13", "ATTR14", "ATTR15" };

	static String[] keys = null;

	public CgScanner() {
		super();

		super.language = CgScanner.language;
		super.types = CgScanner.types;
		super.functions = CgScanner.functions;
		super.semantics = CgScanner.semantics;
	}

}
