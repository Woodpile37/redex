/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

package com.facebook.redextest;

import static org.assertj.core.api.Assertions.assertThat;

import com.facebook.annotations.OkToExtend;
import com.facebook.redex.test.instr.KeepForRedexTest;
import org.junit.Test;

@OkToExtend
class Base {
  String foo() { return "Base"; }
  static Base getInstance() { return new Base(); }
}

@OkToExtend
class SubOne extends Base {
  static Base getInstance() { return new SubOne(); }
}

@OkToExtend
class SubTwo extends SubOne {
  @Override
  String foo() { return "SubTwo"; }
  static Base getInstance() { return new SubTwo(); }
}

class SubThree extends SubTwo {
  @Override
  String foo() { return "SubThree"; }
  static Base getInstance() { return new SubThree(); }
}

interface Intf {
  Intf getInstance(int i);
  String foo();
}

class Impl implements Intf {
  @Override
  public Intf getInstance(int i) {
    if (i != 0) {
      return this;
    }

    return this;
  }

  @Override
  public String foo() {
    return "Impl";
  }
}

// Rtype specialization virtual subclass collision
interface Animal {
  String getName();
}
class Cat implements Animal {
  @Override
  public String getName() {
    return "Cat";
  }
  Animal foo() {
    return new Cat();
  } // I think you'd specialize this to "Cat foo()"
  Animal bar() { return foo(); }
}
@OkToExtend
class NotYourFavoriteCat extends Cat {
  @Override
  public String getName() {
    return "NotYourFavoriteCat";
  }
  Cat foo() { return new NotYourFavoriteCat(); }
}

// Resolve pure ref to interface method def
interface Concept {
  Concept getReal();
  String getVal();
}
abstract class Incomplete implements Concept {
  Concept getFake() { return getReal(); }
}
final class Complete extends Incomplete {
  @Override
  public Concept getReal() {
    return this;
  }
  @Override
  public String getVal() {
    return "Complete";
  }
}

final class Copiable {
  final int val;
  Copiable(int v) { this.val = v; }
  Copiable[] copyToArray() {
    Copiable[] arr = {this};
    Copiable[] copy = arr.clone();
    return copy;
  }
}

/*
 * Rtype specialization for input with overridding miranda methods
 */
interface Mad {
  Animal baz();
}
class MadMan implements Mad {
  @Override
  public Animal baz() {
    return new Cat();
  }
}
class MadWoman implements Mad {
  @Override
  public Animal baz() {
    return new Cat();
  }
}
interface Crazy {
  Animal baz();
}
@OkToExtend
class CrazyPerson extends MadMan implements Crazy {
  // no impl. so fall back to super.
}

@KeepForRedexTest
public class ResolveRefsTest {

  @KeepForRedexTest
  @Test
  public void testSimpleInvokeVirtual() {
    Base b = new Base();
    assertThat(b.foo()).isEqualTo("Base");
    SubOne s1 = new SubOne();
    assertThat(s1.foo()).isEqualTo("Base");
    SubTwo s2 = new SubTwo();
    assertThat(s2.foo()).isEqualTo("SubTwo");
    SubThree s3 = new SubThree();
    assertThat(s3.foo()).isEqualTo("SubThree");
  }

  @KeepForRedexTest
  @Test
  public void testFactoryBaseInvokeVirtual() {
    Base b = Base.getInstance();
    assertThat(b.foo()).isEqualTo("Base");
    b = SubOne.getInstance();
    assertThat(b.foo()).isEqualTo("Base");
    b = SubTwo.getInstance();
    assertThat(b.foo()).isEqualTo("SubTwo");
    b = SubThree.getInstance();
    assertThat(b.foo()).isEqualTo("SubThree");
  }

  @KeepForRedexTest
  @Test
  public void testFactoryCastInvokeVirtual() {
    SubOne s1 = (SubOne) SubOne.getInstance();
    assertThat(s1.foo()).isEqualTo("Base");
    SubTwo s2 = (SubTwo) SubTwo.getInstance();
    assertThat(s2.foo()).isEqualTo("SubTwo");
    SubTwo s3 = (SubThree) SubThree.getInstance();
    assertThat(s3.foo()).isEqualTo("SubThree");
  }

  @KeepForRedexTest
  @Test
  public void testSimpleRTypeSpecialization() {
    Intf i = new Impl();
    Intf ii = i.getInstance(1);
    assertThat(ii.foo()).isEqualTo("Impl");
  }

  @KeepForRedexTest
  @Test
  public void testRTypeSpecializationCollision() {
    Cat c = new NotYourFavoriteCat();
    assertThat(c.getName()).isEqualTo("NotYourFavoriteCat");
    Animal bar = c.bar();
    assertThat(bar.getName()).isEqualTo("NotYourFavoriteCat");
  }

  @KeepForRedexTest
  @Test
  public void testResolveMirandaToInterface() {
    Concept c = new Complete();
    assertThat(c.getReal().getVal()).isEqualTo("Complete");
    Incomplete i = new Complete();
    assertThat(i.getFake().getVal()).isEqualTo("Complete");
  }

  @KeepForRedexTest
  @Test
  public void testResolveObjectArrayClone() {
    Copiable c = new Copiable(42);
    Copiable[] arrCopy = c.copyToArray();
    assertThat(arrCopy.length).isEqualTo(1);
    assertThat(arrCopy[0].val).isEqualTo(42);
  }

  @KeepForRedexTest
  @Test
  public void testRTypeSpecializationOnMirandaOverridden() {
    Mad m = new MadMan();
    assertThat(m.baz().getName()).isEqualTo("Cat");
    m = new MadWoman();
    assertThat(m.baz().getName()).isEqualTo("Cat");
    Crazy c = new CrazyPerson();
    assertThat(c.baz().getName()).isEqualTo("Cat");
  }
}
