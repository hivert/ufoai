/*
 * generated by Xtext
 */
package net.sourceforge.ufoai.ui.labeling;

import net.sourceforge.ufoai.ufoScript.UFONode;
import net.sourceforge.ufoai.ui.UFOScriptUiModule;

import org.eclipse.emf.edit.ui.provider.AdapterFactoryLabelProvider;
import org.eclipse.xtext.ui.label.DefaultEObjectLabelProvider;

import com.google.inject.Inject;

/**
 * Provides labels for a EObjects. see
 * http://www.eclipse.org/Xtext/documentation/latest/xtext.html#labelProvider
 */
public class UFOScriptLabelProvider extends DefaultEObjectLabelProvider {

	@Inject
	public UFOScriptLabelProvider(AdapterFactoryLabelProvider delegate) {
		super(delegate);
	}

	public String text(UFONode node) {
		if (node.getName() != null) {
			return node.getType() + " " + node.getName();
		} else {
			return node.getType();
		}
	}

	@Override
	public Object doGetImage(Object block) {
		if (block instanceof UFONode) {
			return UFOScriptUiModule.getImageDescriptor("icons/" + ((UFONode) block).getType() + ".png");
		}
		return super.doGetImage(block);
	}
}
