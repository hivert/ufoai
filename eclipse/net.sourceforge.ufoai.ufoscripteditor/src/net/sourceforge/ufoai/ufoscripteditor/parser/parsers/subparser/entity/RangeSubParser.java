package net.sourceforge.ufoai.ufoscripteditor.parser.parsers.subparser.entity;

import net.sourceforge.ufoai.ufoscripteditor.parser.IParserContext;
import net.sourceforge.ufoai.ufoscripteditor.parser.parsers.subparser.AbstractUFOSubParser;
import net.sourceforge.ufoai.ufoscripteditor.parser.parsers.subparser.IUFOSubParser;
import net.sourceforge.ufoai.ufoscripteditor.parser.parsers.subparser.IUFOSubParserFactory;
import net.sourceforge.ufoai.ufoscripteditor.parser.parsers.subparser.UFOSubParserFactoryAdapter;

public class RangeSubParser extends AbstractUFOSubParser {
	public static final IUFOSubParserFactory FACTORY = new UFOSubParserFactoryAdapter() {
		@Override
		public String getID() {
			return "range";
		}

		@Override
		public IUFOSubParser create(IParserContext ctx) {
			return new RangeSubParser(ctx);
		}

		@Override
		public boolean isIDName() {
			return true;
		}
	};

	public RangeSubParser(IParserContext ctx) {
		super(ctx);
	}
}
